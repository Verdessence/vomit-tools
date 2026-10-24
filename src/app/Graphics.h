//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the previews draw with: a graphics API through Qt's RHI (Vulkan first where it
//          works), or the CPU. It is settled once per run from the preference in Edit >
//          Preferences; VOMIT_TOOLS_GRAPHICS (auto, vulkan, opengl, d3d11, d3d12, metal, software)
//          overrides it, for tests.
//
//===========================================================================//
#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <QString>

#ifdef VOMIT_TOOLS_GPU
#include <QRhiWidget>

class QOffscreenSurface;
class QRhi;
class QVulkanInstance;
#endif

enum class GraphicsApi_t
{
	GRAPHICS_API_AUTO, // Vulkan where it works, else the platform's usual API, else the CPU
	GRAPHICS_API_VULKAN,
	GRAPHICS_API_OPENGL,
	GRAPHICS_API_DIRECT3D11,
	GRAPHICS_API_DIRECT3D12,
	GRAPHICS_API_METAL,
	GRAPHICS_API_SOFTWARE, // no graphics card: the previews draw on the CPU
};

// How the preference is stored: "vulkan".
[[nodiscard]] std::string_view GraphicsApiKey( GraphicsApi_t nApi );
[[nodiscard]] std::optional<GraphicsApi_t> GraphicsApiFromKey( std::string_view szKey );
// "Vulkan", "Software (CPU)".
[[nodiscard]] QString GraphicsApiName( GraphicsApi_t nApi );

class CGraphics
{
public:
	// Settled on first use, which needs the application to exist.
	[[nodiscard]] static CGraphics &Instance();

	// What this run draws with; never GRAPHICS_API_AUTO.
	[[nodiscard]] GraphicsApi_t GetActive() const { return m_nActive; }
	[[nodiscard]] bool IsGpu() const { return m_nActive != GraphicsApi_t::GRAPHICS_API_SOFTWARE; }
	// "Vulkan on NVIDIA GeForce RTX 3060 Laptop GPU", "Software (CPU)".
	[[nodiscard]] const QString &GetDescription() const { return m_szDescription; }
	// Why the API asked for isn't the one used; empty when it is.
	[[nodiscard]] const QString &GetProblem() const { return m_szProblem; }
	// The APIs this platform and build can offer, Auto and Software included.
	[[nodiscard]] static std::vector<GraphicsApi_t> Choices();

#ifdef VOMIT_TOOLS_GPU
	[[nodiscard]] QRhiWidget::Api GetWidgetApi() const;
	// A QRhi of the active API for drawing into textures (thumbnails); nullptr in software or when
	// it can't be made.
	[[nodiscard]] std::unique_ptr<QRhi> CreateOffscreen();
#endif

	~CGraphics();

private:
	CGraphics();
	// Whether api works here, with the reason when it doesn't; fills the description when it does.
	bool TryApi( GraphicsApi_t nApi, QString &szWhyNot );

	GraphicsApi_t m_nActive = GraphicsApi_t::GRAPHICS_API_SOFTWARE;
	QString m_szDescription;
	QString m_szProblem;
#ifdef VOMIT_TOOLS_GPU
	std::unique_ptr<QVulkanInstance> m_pVulkan;
	std::unique_ptr<QOffscreenSurface> m_pSurface; // OpenGL's QRhis draw through it
#endif
};
