//! Native application dialogs use the engine's preference metadata and export pipeline.
use crate::Bridge;
use photocraft_doc::{Document, LayerId};
use photocraft_engine::{Session, prefs};
use serde_json::{Value, json};

fn fields(value: &Value, path: &str, out: &mut Vec<Value>) {
    if prefs::is_hidden(path) || prefs::LINUX_ONLY.contains(&path) {
        return;
    }
    if let Some(object) = value.as_object() {
        for (key, value) in object {
            fields(value, &format!("{path}.{key}"), out);
        }
        return;
    }
    if !value.is_boolean() && !value.is_number() && !value.is_string() {
        return;
    }
    let (lo, hi) = prefs::range(path).unwrap_or((-1e6, 1e6));
    out.push(json!({"path":path,"key":path.rsplit('.').next().unwrap_or(path),"value":value,"choices":prefs::choices(path),"min":lo,"max":hi,"color":prefs::is_color(path)}));
}
impl Bridge {
    /// Apply the frontend's authority to nested action/batch steps as well as the outer command.
    /// Native sessions have no permanent authorizer; an existing one is never replaced.
    pub fn execute_authorized(&mut self, id: &str, params: &str, authorize: fn(&str, &Value) -> photocraft_engine::Result<()>) -> Result<String, String> {
        if self.session.authorize.is_some() {
            return Err("session already has an authorizer".into());
        }
        self.session.authorize = Some(authorize);
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| self.execute(id, params)));
        self.session.authorize = None;
        result.unwrap_or_else(|_| Err("internal authorized command error".into()))
    }
    /// Capability readers hand bytes to the frontend; no filesystem lookup occurs here.
    pub fn open_data(&mut self, name: &str, path: &str, bytes: &[u8]) -> Result<String, String> {
        let r = photocraft_io::import(name, bytes).map_err(|e| e.to_string())?;
        self.live = None;
        self.session.add_document(r.document, (!r.source_read_only).then(|| path.to_string()));
        if let Some(st) = self.session.active_mut() {
            st.source_read_only = r.source_read_only;
        }
        Ok(serde_json::json!({"warnings":r.warnings}).to_string())
    }
    pub fn encode_data(&self, name: &str) -> Result<(Vec<u8>, String), String> {
        let st = self.session.active().ok_or("no document open")?;
        photocraft_io::export(&st.doc, name, &photocraft_io::ExportOptions::default())
            .map(|r| (r.bytes, serde_json::json!(r.warnings).to_string()))
            .map_err(|e| e.to_string())
    }
    pub fn mark_data_saved(&mut self, path: &str) -> Result<(), String> {
        let st = self.session.active_mut().ok_or("no document open")?;
        st.path = Some(path.to_string());
        st.saved_revision = st.revision;
        if let Some(name) = std::path::Path::new(path).file_name() {
            std::sync::Arc::make_mut(&mut st.doc).name = name.to_string_lossy().into_owned();
        }
        Ok(())
    }
    pub(crate) fn shortcut_dialog(&mut self, p: &Value, apply: bool) -> Result<Value, String> {
        let mut scratch = Session::new();
        scratch.load_prefs_json(&self.session.prefs_to_json())?;
        if let Some(overrides) = p.get("overrides") {
            scratch.execute("edit.keyboardShortcuts", json!({"reset":true,"set":overrides})).map_err(|e| e.to_string())?;
        }
        let mut change = p.get("change").cloned().unwrap_or_else(|| json!({}));
        change["list"] = json!(true);
        let result = scratch.execute("edit.keyboardShortcuts", change).map_err(|e| e.to_string())?;
        // Vetro's Windows keyboard uses Ctrl for the engine's platform Command key.
        // Normalize imported Windows .kys keys before resolving conflicts with Cmd defaults.
        let changed: serde_json::Map<String, Value> = scratch
            .prefs()
            .shortcuts
            .iter()
            .filter_map(|(id, key)| {
                let normalized = prefs::normalize_shortcut(key)?;
                let native = if let Some(rest) = normalized.strip_prefix("Cmd+Ctrl+") {
                    format!("Cmd+{rest}")
                } else if let Some(rest) = normalized.strip_prefix("Ctrl+") {
                    format!("Cmd+{rest}")
                } else {
                    normalized
                };
                (native != *key).then(|| (id.clone(), json!(native)))
            })
            .collect();
        let result = if changed.is_empty() {
            result
        } else {
            let mut next = scratch.execute("edit.keyboardShortcuts", json!({"set":changed,"list":true})).map_err(|e| e.to_string())?;
            if let Some(import) = result.get("import") {
                next["import"] = import.clone();
            }
            next
        };
        if apply {
            self.session.load_prefs_json(&scratch.prefs_to_json())?;
        }
        Ok(result)
    }
    pub fn preferences_info(&self) -> String {
        let saved = self.session.prefs_value();
        let sections: Vec<_> = prefs::SECTIONS
            .iter()
            .map(|(id, title)| {
                let mut entries = Vec::new();
                fields(&saved[id], id, &mut entries);
                json!({"id":id,"title":title,"fields":entries})
            })
            .collect();
        json!({"saved":saved,"sections":sections}).to_string()
    }
    pub fn restore_preferences(&mut self, serialized: &str) -> Result<(), String> {
        if serialized.len() > 16 * 1024 * 1024 {
            return Err("preferences exceed 16 MiB".into());
        }
        self.session.load_prefs_json(serialized)
    }
    fn export_document(&self, p: &Value) -> Result<Document, String> {
        let state = self.session.active().ok_or("no document")?;
        if p["onlyLayer"].as_bool().unwrap_or(false) {
            let id = p["layer"].as_u64().map(LayerId).or(state.active_layer).ok_or("no layer")?;
            photocraft_engine::layer_menu_cmds::layer_document(&state.doc, id).map_err(|e| e.to_string())
        } else {
            Ok((*state.doc).clone())
        }
    }
    pub(crate) fn export_layer_dialog(&self, p: &Value) -> Result<Value, String> {
        let mut scratch = Session::new();
        scratch.load_prefs_json(&self.session.prefs_to_json())?;
        scratch.add_document(self.export_document(p)?, None);
        scratch.execute("file.export.saveForWebLegacy", p.clone()).map_err(|e| e.to_string())
    }
    pub(crate) fn export_preview_document(&self, p: &Value, side: u32) -> Result<Document, String> {
        let doc = self.export_document(p)?;
        let width = p["width"].as_f64().unwrap_or(f64::from(doc.size.width)).max(1.);
        let height = p["height"].as_f64().unwrap_or(f64::from(doc.size.height)).max(1.);
        let factor = (f64::from(side) / width.max(height)).min(1.);
        let filename = match p["format"].as_str() {
            Some("jpeg") => "preview.jpg",
            Some("gif") => "preview.gif",
            _ => "preview.png",
        };
        let mut params = p.clone();
        let object = params.as_object_mut().ok_or("export params must be an object")?;
        object.insert("width".into(), json!((width * factor).round().max(1.)));
        object.insert("height".into(), json!((height * factor).round().max(1.)));
        object.insert("path".into(), json!(filename));
        object.insert("html".into(), json!(false));
        let mut scratch = Session::new();
        scratch.load_prefs_json(&self.session.prefs_to_json())?;
        scratch.add_document(doc, None);
        let mut encoded = Vec::new();
        photocraft_engine::web_cmds::save_for_web_with_writer(&mut scratch, &params, &mut |_, bytes| {
            encoded = bytes.to_vec();
            Ok(())
        })
        .map_err(|e| e.to_string())?;
        // Sniffing the bytes selects the codec; no file is created for a preview.
        photocraft_io::import(filename, &encoded).map(|r| r.document).map_err(|e| e.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn shortcut_preview_and_failed_apply_leave_saved_bindings_intact() {
        let mut b = Bridge::new();
        b.execute("edit.keyboardShortcuts", r#"{"set":{"edit.undo":"F9"}}"#).unwrap();
        let before = b.preferences_info();
        let result = b.execute("vetro.shortcuts.preview", r#"{"change":{"set":{"edit.undo":"Ctrl+Alt+U"}}}"#).unwrap();
        assert!(result.contains("Cmd+Alt+U"));
        assert_eq!(before, b.preferences_info());
        assert!(b.execute("vetro.shortcuts.apply", r#"{"overrides":{"invalid.command":"F7"}}"#).is_err());
        assert_eq!(before, b.preferences_info());
        b.execute("vetro.shortcuts.apply", r#"{"change":{"set":{"edit.undo":"Ctrl+Alt+U"}}}"#).unwrap();
        assert_eq!(b.session.prefs().shortcuts.get("edit.undo").map(String::as_str), Some("Cmd+Alt+U"));
    }
    #[test]
    fn native_preferences_use_engine_choices_and_survive_restore() {
        let mut b = Bridge::new();
        b.execute("prefs.set", r#"{"path":"guidesGridAndSlices.subdivisions","value":7}"#).unwrap();
        b.execute("edit.keyboardShortcuts", r#"{"set":{"edit.undo":"F9"}}"#).unwrap();
        let info: Value = serde_json::from_str(&b.preferences_info()).unwrap();
        assert_eq!(info["sections"].as_array().unwrap().len(), 18);
        let entries = info["sections"].as_array().unwrap().iter().flat_map(|s| s["fields"].as_array().unwrap());
        assert!(entries.into_iter().any(|f| f["path"] == "guidesGridAndSlices.gridStyle" && f["choices"].as_array().unwrap().len() == 3));
        let mut next = Bridge::new();
        next.restore_preferences(&info["saved"].to_string()).unwrap();
        assert_eq!(next.session.prefs().guides_grid_and_slices.subdivisions, 7);
        assert_eq!(next.session.prefs().shortcuts.get("edit.undo").map(String::as_str), Some("F9"));
    }
    #[test]
    fn export_preview_is_encoded_bounded_and_does_not_edit_the_session() {
        let mut b = Bridge::new();
        b.execute("file.new", r##"{"width":64,"height":48,"background":"#ff0000"}"##).unwrap();
        let before = b.document_info();
        let history = b.history_len();
        for format in ["png24", "jpeg", "gif"] {
            let preview = b.export_preview_document(&json!({"width":640,"height":480,"format":format,"quality":20}), 32).unwrap();
            assert_eq!((preview.size.width, preview.size.height), (32, 24));
            let pixel = crate::sample(&preview, 16, 12);
            assert!(pixel[0] > 0.8 && pixel[1] < 0.1, "{format}: {pixel:?}");
        }
        assert_eq!(before, b.document_info());
        assert_eq!(history, b.history_len());
    }
}

#[cfg(test)]
mod capability_tests {
    use crate::Bridge;
    #[test]
    fn memory_files_round_trip_without_a_filesystem_lookup() {
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":32,"height":24,"background":"white"}"#).unwrap();
        let history = b.history_len();
        for name in ["virtual.pcraft", "virtual.png", "virtual.psd"] {
            let (bytes, _) = b.encode_data(name).unwrap();
            assert!(!bytes.is_empty());
            let mut other = Bridge::new();
            other.open_data(name, name, &bytes).unwrap();
            let doc = other.snapshot().unwrap();
            assert_eq!((doc.size.width, doc.size.height), (32, 24));
        }
        assert_eq!(b.history_len(), history);
        assert!(b.open_data("bad.png", "bad.png", b"bad").is_err());
    }
    #[test]
    fn nested_actions_obey_frontend_authority_and_the_scope_is_restored() {
        fn deny_delete(id: &str, _: &serde_json::Value) -> photocraft_engine::Result<()> {
            if id == "layer.delete" { Err(photocraft_engine::EngineError::Other("denied by frontend".into())) } else { Ok(()) }
        }
        let mut b = Bridge::new();
        b.execute("file.new", r#"{"width":16,"height":16}"#).unwrap();
        b.execute("layer.new.layer", "{}").unwrap();
        b.session.actions.list.push(photocraft_engine::actions_cmds::Action {
            name: "Nested".into(),
            steps: vec![("layer.delete".into(), serde_json::json!({}))],
            ..Default::default()
        });
        let result = b.execute_authorized("actions.play", r#"{"action":"Nested"}"#, deny_delete).unwrap();
        let result: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert!(result["failed"]["error"].as_str().unwrap().contains("denied by frontend"));
        assert_eq!(b.snapshot().unwrap().layers.len(), 2);
        assert!(b.session.authorize.is_none());
        b.execute("layer.delete", "{}").unwrap();
        assert_eq!(b.snapshot().unwrap().layers.len(), 1);
    }
}
