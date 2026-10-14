//! The GPU canvas: the engine's wgpu compositor (`photocraft-gpu`) on Direct3D 12, drawing the
//! document into a texture the shell's Direct3D 11 device opens and Direct2D shows.
//!
//! The texture is created here, on the D3D12 side, as a shared committed resource with
//! simultaneous access: such a resource decays to the common state after every submission, so
//! the other API may read it without a hand-off transition, and it is never compressed. The
//! shell opens the NT handle with `ID3D11Device1::OpenSharedResource1`. Every render waits for
//! the GPU before returning (it runs on the shell's render thread), so what the shell draws next
//! is complete; a frame drawn while the next damage rect is being written shows old or new pixels
//! there, never anything else.
//!
//! Pixels are written exactly as the CPU path writes them (`crate::render_view`): straight float
//! composite → optional sRGB encoding → premultiplied BGRA8.
#![allow(unsafe_code)]

use photocraft_doc::Document;
use photocraft_geom::Rect;
use windows::Win32::Foundation::GENERIC_ALL;
use windows::Win32::Graphics::Direct3D12::{
    D3D12_HEAP_FLAG_SHARED, D3D12_HEAP_PROPERTIES, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_DESC, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_RESOURCE_STATE_COMMON, D3D12_TEXTURE_LAYOUT_UNKNOWN,
    ID3D12Resource,
};
use windows::Win32::Graphics::Dxgi::Common::{DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_SAMPLE_DESC};
use windows::core::PCWSTR;

type Dx12 = wgpu::hal::api::Dx12;

/// Largest side Direct3D 11 can open, and the largest canvas this path takes on (pixels): above
/// it the CPU path's reduced levels are the better tool.
pub const MAX_SIDE: u32 = 16384;
pub const MAX_PIXELS: u64 = 120_000_000;

const SHADER: &str = r"
struct Params { offset: vec2<i32>, encode: i32, pad: i32 };
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var<uniform> p: Params;

@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
  let x = f32((i << 1u) & 2u);
  let y = f32(i & 2u);
  return vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}

fn srgb(c: vec3<f32>) -> vec3<f32> {
  return select(1.055 * pow(c, vec3<f32>(1.0 / 2.4)) - 0.055, c * 12.92, c <= vec3<f32>(0.0031308));
}

@fragment fn fs(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
  var c = textureLoad(src, vec2<i32>(floor(pos.xy)) - p.offset, 0);
  if (any(c != c)) { c = vec4<f32>(0.0); }
  var rgb = clamp(c.rgb, vec3<f32>(0.0), vec3<f32>(1.0));
  if (p.encode != 0) { rgb = srgb(rgb); }
  let a = clamp(c.a, 0.0, 1.0);
  return vec4<f32>(rgb * a, a);
}
";

/// What a render did.
pub enum Outcome {
    Done,
    /// The GPU compositor doesn't cover this document (yet): the CPU path takes over.
    Unsupported(String),
}

struct Target {
    view: wgpu::TextureView,
    width: u32,
    height: u32,
}

pub struct Gpu {
    device: wgpu::Device,
    queue: wgpu::Queue,
    comp: photocraft_gpu::Compositor,
    pipeline: wgpu::RenderPipeline,
    layout: wgpu::BindGroupLayout,
    target: Option<Target>,
}

/// The adapter's LUID, as DXGI reports it (what the shell's D3D11 device was created on).
fn adapter_luid(a: &wgpu::Adapter) -> Option<(u32, i32)> {
    // SAFETY: the hal adapter is only read (its DXGI description), while `a` is alive.
    let hal = unsafe { a.as_hal::<Dx12>() }?;
    // SAFETY: GetDesc1 writes a plain description struct.
    let desc = unsafe { hal.raw_adapter().GetDesc1() }.ok()?;
    Some((desc.AdapterLuid.LowPart, desc.AdapterLuid.HighPart))
}

impl Gpu {
    /// A device on the adapter with this LUID (the shell's), and the compositor.
    pub fn new(luid: (u32, i32)) -> Result<Self, String> {
        let mut desc = wgpu::InstanceDescriptor::new_without_display_handle();
        desc.backends = wgpu::Backends::DX12;
        let instance = wgpu::Instance::new(desc);
        let adapters = pollster::block_on(instance.enumerate_adapters(wgpu::Backends::DX12));
        let adapter = adapters.into_iter().find(|a| adapter_luid(a) == Some(luid)).ok_or("no Direct3D 12 adapter matches the window's")?;
        let a = adapter.limits();
        let limits = wgpu::Limits { max_texture_dimension_2d: a.max_texture_dimension_2d, max_buffer_size: a.max_buffer_size, ..wgpu::Limits::default() };
        let (device, queue) = pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("photocraft vetro canvas"),
            required_limits: limits,
            ..Default::default()
        }))
        .map_err(|e| format!("no GPU device: {e}"))?;
        let comp =
            photocraft_gpu::Compositor::try_new_with_format(&device, photocraft_gpu::Compositor::preferred_acc_format(&adapter)).map_err(|e| e.to_string())?;
        let module =
            device.create_shader_module(wgpu::ShaderModuleDescriptor { label: Some("pc_vetro_encode"), source: wgpu::ShaderSource::Wgsl(SHADER.into()) });
        let layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("pc_vetro_encode"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: false },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None },
                    count: None,
                },
            ],
        });
        let pl = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("pc_vetro_encode"),
            bind_group_layouts: &[Some(&layout)],
            immediate_size: 0,
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("pc_vetro_encode"),
            layout: Some(&pl),
            vertex: wgpu::VertexState { module: &module, entry_point: Some("vs"), compilation_options: Default::default(), buffers: &[] },
            fragment: Some(wgpu::FragmentState {
                module: &module,
                entry_point: Some("fs"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState { format: wgpu::TextureFormat::Bgra8Unorm, blend: None, write_mask: wgpu::ColorWrites::ALL })],
            }),
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview_mask: None,
            cache: None,
        });
        Ok(Self { device, queue, comp, pipeline, layout, target: None })
    }

    /// A new `width`×`height` canvas texture; returns its NT handle, which the caller owns (and
    /// closes once it has opened it). The previous texture is released.
    pub fn create_target(&mut self, width: u32, height: u32) -> Result<*mut std::ffi::c_void, String> {
        self.target = None;
        if width == 0 || height == 0 || width > MAX_SIDE || height > MAX_SIDE || u64::from(width) * u64::from(height) > MAX_PIXELS {
            return Err(format!("{width}×{height} is outside the GPU canvas"));
        }
        // SAFETY: the raw device is only used to create and share one resource below, while the
        // wgpu device is alive.
        let hal = unsafe { self.device.as_hal::<Dx12>() }.ok_or("not a Direct3D 12 device")?;
        let raw = hal.raw_device();
        let heap = D3D12_HEAP_PROPERTIES { Type: D3D12_HEAP_TYPE_DEFAULT, ..Default::default() };
        let desc = D3D12_RESOURCE_DESC {
            Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            Alignment: 0,
            Width: u64::from(width),
            Height: height,
            DepthOrArraySize: 1,
            MipLevels: 1,
            Format: DXGI_FORMAT_B8G8R8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Layout: D3D12_TEXTURE_LAYOUT_UNKNOWN,
            Flags: D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
        };
        let mut resource: Option<ID3D12Resource> = None;
        // SAFETY: valid descriptors on the stack; the out pointer is a live Option.
        unsafe { raw.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON, None, &mut resource) }
            .map_err(|e| format!("canvas texture: {e}"))?;
        let resource = resource.ok_or("canvas texture: no resource")?;
        // SAFETY: `resource` is a live shareable resource of this device.
        let handle = unsafe { raw.CreateSharedHandle(&resource, None, GENERIC_ALL.0, PCWSTR::null()) }.map_err(|e| format!("canvas texture handle: {e}"))?;
        drop(hal);
        let size = wgpu::Extent3d { width, height, depth_or_array_layers: 1 };
        // SAFETY: `resource` was created on this device with exactly this format, size, one mip
        // and one sample.
        let hal_texture =
            unsafe { wgpu::hal::dx12::Device::texture_from_raw(resource, wgpu::TextureFormat::Bgra8Unorm, wgpu::TextureDimension::D2, size, 1, 1) };
        // SAFETY: created from this device's raw handle above, matching the descriptor; it is in
        // the common state, which wgpu treats as uninitialized.
        let texture = unsafe {
            self.device.create_texture_from_hal::<Dx12>(
                hal_texture,
                &wgpu::TextureDescriptor {
                    label: Some("pc_vetro_canvas"),
                    size,
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: wgpu::TextureDimension::D2,
                    format: wgpu::TextureFormat::Bgra8Unorm,
                    usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
                    view_formats: &[],
                },
                wgpu::TextureUses::UNINITIALIZED,
            )
        };
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        self.target = Some(Target { view, width, height });
        Ok(handle.0)
    }

    /// Composite `region` of `doc` into the canvas texture and wait for the GPU.
    pub fn render(&mut self, doc: &Document, region: Rect, encode: bool) -> Result<Outcome, String> {
        let Some(t) = &self.target else { return Err("no canvas texture".into()) };
        if doc.size.width != t.width || doc.size.height != t.height {
            return Err("the canvas texture is another size".into());
        }
        let region = region.intersect(&doc.bounds());
        if region.is_empty() {
            return Ok(Outcome::Done);
        }
        if let Err(e) = self.comp.supports(doc) {
            return Ok(Outcome::Unsupported(e.to_string()));
        }
        let (device, queue, pipeline, layout, view) = (&self.device, &self.queue, &self.pipeline, &self.layout, &t.view);
        if region == doc.bounds() {
            // A whole new picture: start from transparent (also initialises a new texture).
            let mut enc = device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("pc_vetro_clear") });
            drop(enc.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("pc_vetro_clear"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view,
                    resolve_target: None,
                    depth_slice: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT), store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            }));
            queue.submit([enc.finish()]);
        }
        let encode_flag = i32::from(encode);
        let result = self.comp.render(device, queue, doc, region, |enc, out| {
            let r = out.rect.intersect(&region);
            if r.is_empty() {
                return;
            }
            let bytes: Vec<u8> = [out.rect.x0, out.rect.y0, encode_flag, 0].iter().flat_map(|v| v.to_le_bytes()).collect();
            let ubuf = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("pc_vetro_encode"),
                size: 16,
                usage: wgpu::BufferUsages::UNIFORM,
                mapped_at_creation: true,
            });
            if let Ok(mut m) = ubuf.slice(..).get_mapped_range_mut() {
                m.copy_from_slice(&bytes);
            }
            ubuf.unmap();
            let bg = device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some("pc_vetro_encode"),
                layout,
                entries: &[
                    wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(out.view) },
                    wgpu::BindGroupEntry { binding: 1, resource: ubuf.as_entire_binding() },
                ],
            });
            let mut pass = enc.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("pc_vetro_encode"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view,
                    resolve_target: None,
                    depth_slice: None,
                    ops: wgpu::Operations { load: wgpu::LoadOp::Load, store: wgpu::StoreOp::Store },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            pass.set_pipeline(pipeline);
            pass.set_bind_group(0, &bg, &[]);
            pass.set_scissor_rect(r.x0.max(0) as u32, r.y0.max(0) as u32, r.width(), r.height());
            pass.draw(0..3, 0..1);
        });
        if let Err(e) = result {
            return Ok(Outcome::Unsupported(e.to_string()));
        }
        device.poll(wgpu::PollType::Wait { submission_index: None, timeout: None }).map_err(|e| format!("GPU wait: {e}"))?;
        Ok(Outcome::Done)
    }
}
