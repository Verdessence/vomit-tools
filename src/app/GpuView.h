//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Drawing a preview with the graphics card. A renderer draws one picture into a render
//          target; CGpuView shows one in a widget (with the API CGraphics settled on), and
//          COffscreenGpu draws one into an image (thumbnails, and tests that compare it with the
//          software picture).
//
//===========================================================================//
#pragma once

#ifdef VOMIT_TOOLS_GPU

#include <memory>

#include <QImage>
#include <QString>
#include <QRhiWidget>
#include <QSize>

class QRhi;
class QRhiCommandBuffer;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QShader;

class IGpuRenderer
{
public:
	virtual ~IGpuRenderer() = default;
	// Called before the first picture, and again when the QRhi or the target changes (a resize): make
	// what is tied to them (pipelines follow the target's pass and sample count).
	virtual void Initialize( QRhi *pRhi, QRhiRenderTarget *pTarget ) = 0;
	// Draws one picture: begins and ends a pass on the target.
	virtual void Render( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pTarget ) = 0;
	// The QRhi is going away: drop everything made with it.
	virtual void Release() = 0;
};

// A compiled shader from the resources (":/shaders/sky.frag.qsb").
[[nodiscard]] QShader LoadShader( const QString &szPath );

class CGpuView : public QRhiWidget
{
	Q_OBJECT

public:
	CGpuView( std::unique_ptr<IGpuRenderer> pRenderer, QWidget *pParent = nullptr );
	~CGpuView() override;

	[[nodiscard]] IGpuRenderer &GetRenderer() { return *m_pRenderer; }
	// The QRhi backend drawing this widget ("Vulkan", "OpenGL"...), once it has one.
	[[nodiscard]] QString GetBackendName() const;

protected:
	void initialize( QRhiCommandBuffer *pCommands ) override;
	void render( QRhiCommandBuffer *pCommands ) override;
	void releaseResources() override;

private:
	// The renderer works with this QRhi from now on (released with it, should it go first).
	void Attach( QRhi *pRhi );
	void Detach();

	std::unique_ptr<IGpuRenderer> m_pRenderer;
	QRhi *m_pAttached = nullptr; // the QRhi the renderer has its resources in
};

class COffscreenGpu
{
public:
	// With a QRhi of the API CGraphics settled on; when there is none (software), Render() gives
	// nothing.
	[[nodiscard]] static std::unique_ptr<COffscreenGpu> Create( std::unique_ptr<IGpuRenderer> pRenderer );
	~COffscreenGpu();

	[[nodiscard]] bool IsValid() const { return m_pRhi != nullptr; }
	[[nodiscard]] IGpuRenderer &GetRenderer() { return *m_pRenderer; }
	// The renderer's picture at a size in pixels; null when it couldn't be drawn.
	[[nodiscard]] QImage Render( QSize size );

private:
	explicit COffscreenGpu( std::unique_ptr<IGpuRenderer> pRenderer );

	std::unique_ptr<QRhi> m_pRhi;
	std::unique_ptr<IGpuRenderer> m_pRenderer;
	std::unique_ptr<QRhiTexture> m_pColor;
	std::unique_ptr<QRhiRenderBuffer> m_pDepth;
	std::unique_ptr<QRhiTextureRenderTarget> m_pTarget;
	std::unique_ptr<QRhiRenderPassDescriptor> m_pPass;
	QSize m_Size;
};

#endif
