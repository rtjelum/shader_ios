import SwiftUI
import MetalKit

/// Matches `Uniforms` in Sea.msl.
struct Uniforms {
    var resolution: SIMD2<Float>
    var look: SIMD2<Float>
    var time: Float
}

struct ShaderView: UIViewRepresentable {
    func makeCoordinator() -> Renderer { Renderer() }

    func makeUIView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: context.coordinator.device)
        view.delegate = context.coordinator
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = true
        // Run at the screen's full refresh rate (120 Hz on ProMotion, see Info.plist).
        view.preferredFramesPerSecond = UIScreen.main.maximumFramesPerSecond
        // Rendering at full 3x retina is expensive for a ray marcher; 2x looks the same.
        view.contentScaleFactor = min(UIScreen.main.scale, 2)

        // Dragging looks around: sideways turns, up/down tilts.
        let pan = UIPanGestureRecognizer(target: context.coordinator,
                                         action: #selector(Renderer.handlePan(_:)))
        view.addGestureRecognizer(pan)
        return view
    }

    func updateUIView(_ uiView: MTKView, context: Context) {}
}

final class Renderer: NSObject, MTKViewDelegate {
    let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let start = CACurrentMediaTime()
    private var look = SIMD2<Float>(0, 0)
    private var lookAtPanStart = SIMD2<Float>(0, 0)

    override init() {
        device = MTLCreateSystemDefaultDevice()!
        queue = device.makeCommandQueue()!
        // The shader is compiled at launch from Sea.msl, so no offline Metal toolchain is needed.
        let url = Bundle.main.url(forResource: "Sea", withExtension: "msl")!
        let source = try! String(contentsOf: url, encoding: .utf8)
        let options = MTLCompileOptions()
        options.mathMode = .safe  // the hash() noise needs precise sin() on large values
        let library = try! device.makeLibrary(source: source, options: options)
        let desc = MTLRenderPipelineDescriptor()
        desc.vertexFunction = library.makeFunction(name: "fullscreenVertex")
        desc.fragmentFunction = library.makeFunction(name: "seaFragment")
        desc.colorAttachments[0].pixelFormat = .bgra8Unorm
        pipeline = try! device.makeRenderPipelineState(descriptor: desc)
        super.init()
    }

    @objc func handlePan(_ g: UIPanGestureRecognizer) {
        guard let view = g.view else { return }
        if g.state == .began { lookAtPanStart = look }
        // A full screen-width drag turns 360 degrees; dragging the scene follows the finger.
        let t = g.translation(in: view)
        let turn = -Float(t.x / view.bounds.width) * 2 * .pi
        let tilt = Float(t.y / view.bounds.height) * 1.2
        look.x = lookAtPanStart.x + turn
        look.y = min(max(lookAtPanStart.y + tilt, -0.6), 0.6)
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard let pass = view.currentRenderPassDescriptor,
              let drawable = view.currentDrawable,
              let cmd = queue.makeCommandBuffer(),
              let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }

        var u = Uniforms(
            resolution: SIMD2(Float(view.drawableSize.width), Float(view.drawableSize.height)),
            look: look,
            time: Float(CACurrentMediaTime() - start))

        enc.setRenderPipelineState(pipeline)
        enc.setFragmentBytes(&u, length: MemoryLayout<Uniforms>.stride, index: 0)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }
}
