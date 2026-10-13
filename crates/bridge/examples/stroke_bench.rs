//! Times a live brush stroke the way the Vetro shell drives it: one point per mouse move, then the
//! snapshot and the GPU canvas render of the damaged area. `cargo run --release -p
//! photocraft-bridge --example stroke_bench -- <image>`
#![allow(unsafe_code, clippy::unwrap_used, clippy::expect_used)]
use photocraft_bridge::Bridge;
use std::time::Instant;

fn main() {
    let path = std::env::args().nth(1).expect("an image path");
    let mut b = Bridge::new();
    b.open_path(&path).unwrap();
    let doc = b.snapshot().unwrap();
    let (w, h) = (doc.size.width, doc.size.height);
    #[cfg(windows)]
    let mut gpu = {
        let mut desc = wgpu::InstanceDescriptor::new_without_display_handle();
        desc.backends = wgpu::Backends::DX12;
        let inst = wgpu::Instance::new(desc);
        let a = pollster::block_on(inst.enumerate_adapters(wgpu::Backends::DX12)).into_iter().next().unwrap();
        let hal = unsafe { a.as_hal::<wgpu::hal::api::Dx12>() }.unwrap();
        let d = unsafe { hal.raw_adapter().GetDesc1() }.unwrap();
        let mut g = photocraft_bridge::gpu::Gpu::new((d.AdapterLuid.LowPart, d.AdapterLuid.HighPart)).unwrap();
        g.create_target(w, h).unwrap();
        let t = Instant::now();
        let _ = g.render(&doc, doc.bounds(), false);
        println!("gpu full render {:?}", t.elapsed());
        g
    };
    for size in [30.0, 120.0] {
        let p = format!("{{\"points\":[[400,400,1,0,0,0,0]],\"size\":{size},\"hardness\":0.8,\"flow\":1,\"opacity\":1}}");
        b.stroke_begin("paint.stroke", &p).unwrap();
        let (mut tp, mut ts, mut tg) = (0.0, 0.0, 0.0);
        let n = 200;
        let mut held = None;
        for i in 0..n {
            let x = 400.0 + i as f64 * 6.0;
            let y = 400.0 + (i as f64 * 0.1).sin() * 200.0;
            let t = Instant::now();
            let dmg = b.stroke_push(&[[x, y, 1.0, i as f64 * 8.0]]).unwrap();
            tp += t.elapsed().as_secs_f64();
            let t = Instant::now();
            let snap = b.snapshot().unwrap();
            ts += t.elapsed().as_secs_f64();
            #[cfg(windows)]
            {
                let t = Instant::now();
                let _ = gpu.render(&snap, dmg, false);
                tg += t.elapsed().as_secs_f64();
            }
            held = Some(snap); // the canvas thread still holds it when the next point arrives
        }
        drop(held);
        let t = Instant::now();
        b.stroke_end(true).unwrap();
        println!(
            "size {size}: push {:.2} ms, snapshot {:.3} ms, gpu {:.2} ms per move; commit {:?}",
            tp * 1e3 / n as f64,
            ts * 1e3 / n as f64,
            tg * 1e3 / n as f64,
            t.elapsed()
        );
    }
}
