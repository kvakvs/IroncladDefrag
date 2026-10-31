#include "ui/map/D2DRenderer.h"

#include "core/map/World.h"

#include <d2d1.h>
#include <windows.h>
#include <wrl/client.h>

namespace icd::ui {

    using Microsoft::WRL::ComPtr;

    // A tile on the GPU, bound to the render target it was uploaded to.
    struct GpuTile {
        ComPtr<ID2D1Bitmap> bitmap; // The uploaded pixels.
    };

    struct D2DRenderer::Impl {
        HWND window = nullptr; // Target window.
        ComPtr<ID2D1Factory> factory; // Creates the target.
        ComPtr<ID2D1HwndRenderTarget> target; // Created on first use, recreated after a device loss.
        ComPtr<ID2D1SolidColorBrush> brush; // Overlay color; recolored per use, belongs to target.
    };

    namespace {

        // A Direct2D color from 0xAARRGGBB.
        D2D1_COLOR_F colorOf(core::Rgba color) {
            return D2D1::ColorF(static_cast<float>((color >> 16U) & 0xFFU) / 255.0F,
                                static_cast<float>((color >> 8U) & 0xFFU) / 255.0F,
                                static_cast<float>(color & 0xFFU) / 255.0F);
        }

        // A Direct2D rectangle.
        D2D1_RECT_F rectOf(const PixelRect& rect) {
            return D2D1::RectF(static_cast<float>(rect.x), static_cast<float>(rect.y),
                               static_cast<float>(rect.x + rect.width), static_cast<float>(rect.y + rect.height));
        }

        // Opaque BGRA, matching core::Rgba in memory.
        D2D1_PIXEL_FORMAT pixelFormat() {
            return D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
        }

    } // namespace

    D2DRenderer::D2DRenderer(WXHWND window) : impl_(std::make_unique<Impl>()) {
        impl_->window = static_cast<HWND>(window);
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, impl_->factory.GetAddressOf());
    }

    D2DRenderer::~D2DRenderer() = default;

    bool D2DRenderer::begin(wxSize size) {
        if (impl_->factory == nullptr || size.x <= 0 || size.y <= 0) {
            return false;
        }
        const auto pixels = D2D1::SizeU(static_cast<UINT32>(size.x), static_cast<UINT32>(size.y));
        if (impl_->target == nullptr) {
            // 96 DPI: one unit is one physical pixel, whatever the monitor's scale.
            const auto properties =
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, pixelFormat(), 96.0F, 96.0F);
            if (FAILED(impl_->factory->CreateHwndRenderTarget(properties,
                                                              D2D1::HwndRenderTargetProperties(impl_->window, pixels),
                                                              impl_->target.GetAddressOf()))) {
                return false;
            }
            impl_->target->CreateSolidColorBrush(D2D1::ColorF(0.0F, 0.0F, 0.0F), impl_->brush.GetAddressOf());
        }
        else if (impl_->target->GetPixelSize().width != pixels.width ||
                 impl_->target->GetPixelSize().height != pixels.height) {
            impl_->target->Resize(pixels);
        }
        impl_->target->BeginDraw();
        return true;
    }

    void D2DRenderer::clear(core::Rgba color) { impl_->target->Clear(colorOf(color)); }

    GpuTileHandle D2DRenderer::upload(const std::vector<core::Rgba>& pixels) {
        if (impl_->target == nullptr) {
            return nullptr;
        }
        auto tile = std::make_shared<GpuTile>();
        const auto edge = static_cast<UINT32>(core::World::tileSize);
        if (FAILED(impl_->target->CreateBitmap(D2D1::SizeU(edge, edge), pixels.data(), edge * sizeof(core::Rgba),
                                               D2D1::BitmapProperties(pixelFormat()), tile->bitmap.GetAddressOf()))) {
            return nullptr;
        }
        return tile;
    }

    void D2DRenderer::draw(const GpuTileHandle& tile, const PixelRect& destination, const PixelRect& source,
                           bool smooth) {
        if (tile == nullptr || tile->bitmap == nullptr) {
            return;
        }
        const D2D1_RECT_F from = rectOf(source);
        impl_->target->DrawBitmap(
            tile->bitmap.Get(), rectOf(destination), 1.0F,
            smooth ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &from);
    }

    void D2DRenderer::fillRect(const PixelRect& rect, core::Rgba color) {
        if (impl_->brush == nullptr) {
            return;
        }
        impl_->brush->SetColor(colorOf(color));
        impl_->target->FillRectangle(rectOf(rect), impl_->brush.Get());
    }

    void D2DRenderer::strokeRect(const PixelRect& rect, const LineStyle& line) {
        if (impl_->brush == nullptr) {
            return;
        }
        impl_->brush->SetColor(colorOf(line.color));
        impl_->target->DrawRectangle(rectOf(rect), impl_->brush.Get(), line.width);
    }

    bool D2DRenderer::end() {
        const HRESULT result = impl_->target->EndDraw();
        if (result == D2DERR_RECREATE_TARGET) {
            impl_->brush.Reset();
            impl_->target.Reset(); // Device lost: tiles uploaded to the old target are gone too.
            return false;
        }
        return true;
    }

} // namespace icd::ui
