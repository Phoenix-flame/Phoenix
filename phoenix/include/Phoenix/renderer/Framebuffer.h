#pragma once
#include <Phoenix/core/base.h>

namespace Phoenix{
    struct FramebufferSpecification{
		uint32_t Width = 0, Height = 0;
		// FramebufferFormat Format = 
		// > 1: render multisampled (MSAA); Unbind() resolves into the colour texture.
		uint32_t Samples = 1;

		bool SwapChainTarget = false;
	};

	class Framebuffer{
	public:
		virtual ~Framebuffer() = default;

		virtual void Bind() = 0;
		virtual void Unbind() = 0;

		virtual void Resize(uint32_t width, uint32_t height) = 0;
		// MSAA sample count (clamped to what the GPU supports; 1 = off).
		virtual void SetSamples(uint32_t samples) = 0;

		// The resolved (single-sample) colour texture, for display.
		virtual uint32_t GetColorAttachmentRendererID() const = 0;
		// The framebuffer holding that texture: a single-sample target for passes
		// that write the final image directly (e.g. the bloom composite).
		virtual uint32_t GetRendererID() const = 0;

		virtual const FramebufferSpecification& GetSpecification() const = 0;

		static Ref<Framebuffer> Create(const FramebufferSpecification& spec);
		// Highest MSAA sample count the GPU supports.
		static uint32_t GetMaxSamples();
	};
}