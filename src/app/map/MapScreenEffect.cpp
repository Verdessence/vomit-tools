//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapScreenEffect.h.
//
//===========================================================================//
#include "MapScreenEffect.h"

#include <bit>
#include <cstring>
#include <mutex>

#include <QCoreApplication>

#include "shader/Bytecode.h"
#include "shader/Glsl.h"
#include "shader/Vcs.h"

#ifdef VOMIT_TOOLS_GPU
#include <rhi/qshaderbaker.h>

#include "GpuView.h"
#endif

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Tr( const char *pszText )
{
	return QCoreApplication::translate( "CMapScreenEffect", pszText );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The material, its textures (those that aren't the frame), its pixel shader's first
//          combo (screenspace_general uses combo 0) and that written for the card.
//-----------------------------------------------------------------------------
std::shared_ptr<CMapScreenEffect> CMapScreenEffect::Load( const QString &szMaterial, const xv::material::Reader_t &pfnRead, QString *pProblem )
{
	const auto fail = [ pProblem ]( const QString &szWhy ) -> std::shared_ptr<CMapScreenEffect> {
		if ( pProblem )
			*pProblem = szWhy;
		return nullptr;
	};
	QString szName = szMaterial.trimmed().toLower();
	szName.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
	if ( szName.startsWith( QStringLiteral( "materials/" ) ) )
		szName.remove( 0, 10 );
	if ( szName.endsWith( QStringLiteral( ".vmt" ) ) )
		szName.chop( 4 );
	if ( szName.isEmpty() )
		return fail( Tr( "No material named." ) );
	const std::string szPath = "materials/" + szName.toStdString() + ".vmt";
	const auto bytes = pfnRead ? pfnRead( szPath ) : std::nullopt;
	if ( !bytes )
		return fail( Tr( "%1 isn't in the game's files." ).arg( QString::fromStdString( szPath ) ) );
	std::string szError;
	const auto material = xv::material::ParseMaterial( std::string_view{ reinterpret_cast<const char *>( bytes->data() ), bytes->size() }, pfnRead, {}, &szError );
	if ( !material )
		return fail( Tr( "%1 can't be read: %2" ).arg( QString::fromStdString( szPath ), QString::fromStdString( szError ) ) );
	if ( !xv::shader::IsScreenEffect( *material ) )
		return fail( Tr( "%1 is a %2 material, not a screen effect (screenspace_general)." ).arg( szName, QString::fromStdString( material->m_szShader ) ) );
	auto pEffect = std::make_shared<CMapScreenEffect>();
	pEffect->m_szMaterial = szName;
	pEffect->m_pMaterial = std::make_shared<const xv::material::CMaterial>( *material );
	pEffect->m_pProxies = std::make_shared<const xv::material::CProxies>( *material );
	pEffect->m_pTextureSet = std::make_shared<const xv::material::CTextureSet>( xv::material::CTextureSet::Load( *material, pfnRead ) );
	const xv::shader::ScreenEffect_t effect = xv::shader::ReadScreenEffect( *material );
	constexpr std::array<std::string_view, 4> PARAMETERS{ "$basetexture", "$texture1", "$texture2", "$texture3" };
	for ( std::size_t i = 0; i < 4; i++ )
	{
		pEffect->m_FrameSamplers[ i ] = xv::shader::IsFrameTexture( effect.m_Textures[ i ] );
		if ( const xv::material::Texture_t *pTexture = pEffect->m_pTextureSet->Find( PARAMETERS[ i ] ); pTexture && !pTexture->m_Frames.empty() )
			pEffect->m_Textures[ i ] = pTexture->m_Frames.front();
	}
	if ( effect.m_szPixelShader.empty() )
	{
		pEffect->m_szProblem = Tr( "It names no $pixshader." );
		return pEffect;
	}
	const std::vector<std::string> paths = xv::shader::PixelShaderPaths( effect.m_szPixelShader );
	std::optional<std::vector<std::byte>> shader;
	for ( const std::string &szShader : paths )
	{
		if ( ( shader = pfnRead( szShader ) ) )
		{
			pEffect->m_szShaderPath = QString::fromStdString( szShader );
			break;
		}
	}
	if ( !shader )
	{
		pEffect->m_szProblem = Tr( "Its pixel shader, %1, isn't in the game's files: the views show the frame as it is." ).arg( QString::fromStdString( paths.front() ) );
		return pEffect;
	}
	const auto file = xv::shader::ReadVcs( *shader, &szError );
	const xv::shader::VcsCombo_t *pCombo = file ? file->Find( 0, 0 ) : nullptr;
	if ( !pCombo && file && !file->m_Combos.empty() )
		pCombo = &file->m_Combos.front();
	const auto program = pCombo ? xv::shader::ReadBytecode( pCombo->m_Tokens, &szError ) : std::nullopt;
	if ( !program )
	{
		pEffect->m_szProblem = Tr( "%1 can't be read: %2" ).arg( pEffect->m_szShaderPath, QString::fromStdString( szError.empty() ? std::string{ "it has no combos" } : szError ) );
		return pEffect;
	}
	pEffect->m_Shader.emplace( *program );
	if ( !pEffect->m_Shader->CanRun() )
	{
		pEffect->m_szProblem = QString::fromStdString( pEffect->m_Shader->GetProblem() );
		return pEffect;
	}
	if ( const auto glsl = xv::shader::TranslateToGlsl( *program, &szError ) )
		pEffect->m_szGlsl = *glsl;
#ifdef VOMIT_TOOLS_GPU
	else
		pEffect->m_szCardProblem = QString::fromStdString( szError );
#endif
	return pEffect;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapScreenEffect::GetProfile() const
{
	return m_Shader ? QString::fromStdString( m_Shader->GetProgram().Profile() ) : QString{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapScreenEffect::IsAnimated() const
{
	return m_pProxies && m_pProxies->IsAnimated();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::shader::ScreenEffect_t CMapScreenEffect::At( float flTime ) const
{
	if ( !m_pMaterial )
		return {};
	const xv::material::CMaterial now = m_pProxies ? m_pProxies->At( flTime, [ this ]( std::string_view szParameter ) { return m_pTextureSet ? m_pTextureSet->FrameCount( szParameter ) : 0; } ) : *m_pMaterial;
	xv::shader::ScreenEffect_t effect = xv::shader::ReadScreenEffect( now );
	for ( std::size_t i = 0; i < 4; i++ )
		if ( m_Overrides[ i ] )
			effect.m_Constants[ i ] = *m_Overrides[ i ];
	return effect;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<CMapScreenEffect> CMapScreenEffect::WithOverrides( std::array<std::optional<xv::shader::Vec4_t>, 4> overrides ) const
{
	auto pCopy = std::make_shared<CMapScreenEffect>( *this );
	pCopy->m_Overrides = std::move( overrides );
	return pCopy;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapScreenEffect::Apply( xv::texture::CImage &frame, float flTime ) const
{
	if ( !CanRun() || frame.Empty() )
		return;
	std::array<const xv::texture::CImage *, 4> textures{};
	for ( std::size_t i = 0; i < 4; i++ )
		textures[ i ] = m_FrameSamplers[ i ] ? &frame : m_Textures[ i ].Empty() ? nullptr
																				: &m_Textures[ i ];
	if ( auto applied = xv::shader::ApplyScreenEffect( *m_Shader, At( flTime ), frame, textures ) )
		frame = std::move( *applied );
}

#ifdef VOMIT_TOOLS_GPU

namespace
{

// The uniform block as the GLSL declares it (std140: every member a vec4 wide).
struct EffectUniforms_t
{
	float m_Constants[ xv::shader::GLSL_CONSTANTS ][ 4 ];
	std::int32_t m_Integers[ 16 ][ 4 ];
	float m_Bools[ 16 ][ 4 ];
	float m_Screen[ 4 ];
	float m_FrameSampler[ 4 ];
	float m_Srgb[ 4 ];
	float m_Mode[ 4 ];
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: As the map's own shaders are: SPIR-V, and the GLSL, HLSL and MSL the backends take.
//-----------------------------------------------------------------------------
const QShader &CMapScreenEffect::GetBaked() const
{
	static std::mutex mutex;
	const std::lock_guard lock( mutex );
	if ( !m_pBaked )
	{
		m_pBaked = std::make_shared<QShader>();
		if ( !m_szGlsl.empty() )
		{
			QShaderBaker baker;
			baker.setGeneratedShaderVariants( { QShader::StandardShader } );
			baker.setGeneratedShaders( { { QShader::SpirvShader, QShaderVersion( 100 ) }, { QShader::GlslShader, QShaderVersion( 330 ) }, { QShader::GlslShader, QShaderVersion( 300, QShaderVersion::GlslEs ) }, { QShader::HlslShader, QShaderVersion( 50 ) }, { QShader::MslShader, QShaderVersion( 12 ) } } );
			baker.setSourceString( QByteArray::fromStdString( m_szGlsl ), QShader::FragmentStage );
			*m_pBaked = baker.bake();
			if ( !m_pBaked->isValid() )
				m_szCardProblem = Tr( "Its pixel shader doesn't compile for the card: %1" ).arg( baker.errorMessage() );
		}
	}
	return *m_pBaked;
}

//-----------------------------------------------------------------------------
// Purpose: An RGBA8 texture and a depth buffer the scene draws into, as big as the view.
//-----------------------------------------------------------------------------
QRhiRenderTarget *CScreenEffectPass::SceneTarget( QRhi *pRhi, QRhiRenderTarget *pFinal )
{
	const QSize size = pFinal->pixelSize();
	if ( pRhi != m_pRhi )
	{
		*this = CScreenEffectPass{};
		m_pRhi = pRhi;
	}
	if ( m_pTarget && size == m_Size )
		return m_pTarget.get();
	m_Size = size;
	m_pTarget.reset();
	m_pPass.reset();
	m_pScene.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget ) );
	m_pScene->create();
	m_pDepth.reset( m_pRhi->newRenderBuffer( QRhiRenderBuffer::DepthStencil, size, 1 ) );
	m_pDepth->create();
	QRhiTextureRenderTargetDescription description{ QRhiColorAttachment( m_pScene.get() ) };
	description.setDepthStencilBuffer( m_pDepth.get() );
	m_pTarget.reset( m_pRhi->newTextureRenderTarget( description ) );
	m_pPass.reset( m_pTarget->newCompatibleRenderPassDescriptor() );
	m_pTarget->setRenderPassDescriptor( m_pPass.get() );
	m_pTarget->create();
	return m_pTarget.get();
}

//-----------------------------------------------------------------------------
// Purpose: The effect's textures uploaded and its pipeline made the first time it draws (or the
//          view's target or the scene's texture changes); then one triangle over the view.
//-----------------------------------------------------------------------------
bool CScreenEffectPass::Draw( QRhiCommandBuffer *pCommands, QRhiRenderTarget *pFinal, const std::shared_ptr<const CMapScreenEffect> &pEffect, float flTime )
{
	if ( !pEffect || !m_pScene || !pEffect->CanRun() )
		return false;
	const QShader &shader = pEffect->GetBaked();
	if ( !shader.isValid() )
		return false;
	QRhiResourceUpdateBatch *pUpdates = m_pRhi->nextResourceUpdateBatch();
	if ( !m_pUniforms )
	{
		m_pUniforms.reset( m_pRhi->newBuffer( QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof( EffectUniforms_t ) ) );
		m_pUniforms->create();
		m_pSampler.reset( m_pRhi->newSampler( QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge ) );
		m_pSampler->create();
		m_pBlack.reset( m_pRhi->newTexture( QRhiTexture::RGBA8, QSize( 1, 1 ) ) );
		m_pBlack->create();
		QImage black( 1, 1, QImage::Format_RGBA8888 );
		black.fill( Qt::black );
		pUpdates->uploadTexture( m_pBlack.get(), black );
	}
	if ( pEffect != m_pFor )
	{
		// Its own textures, as they are (the frame's are the scene's).
		for ( int i = 0; i < 4; i++ )
		{
			m_Textures[ static_cast<std::size_t>( i ) ].reset();
			const xv::texture::CImage &image = pEffect->GetTexture( i );
			if ( pEffect->IsFrameSampler( i ) || image.Empty() )
				continue;
			auto pTexture = std::unique_ptr<QRhiTexture>( m_pRhi->newTexture( QRhiTexture::RGBA8, QSize( image.m_nWidth, image.m_nHeight ) ) );
			pTexture->create();
			pUpdates->uploadTexture( pTexture.get(), QImage( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, image.m_nWidth * 4, QImage::Format_RGBA8888 ).copy() );
			m_Textures[ static_cast<std::size_t>( i ) ] = std::move( pTexture );
		}
		m_pBindings.reset();
		m_pPipeline.reset();
	}
	if ( !m_pBindings || m_pBoundScene != m_pScene.get() )
	{
		std::vector<QRhiShaderResourceBinding> bindings{ QRhiShaderResourceBinding::uniformBuffer( 0, QRhiShaderResourceBinding::FragmentStage, m_pUniforms.get() ) };
		for ( int i = 0; i < 4; i++ )
		{
			QRhiTexture *pTexture = pEffect->IsFrameSampler( i ) ? m_pScene.get() : m_Textures[ static_cast<std::size_t>( i ) ] ? m_Textures[ static_cast<std::size_t>( i ) ].get()
																																: m_pBlack.get();
			bindings.push_back( QRhiShaderResourceBinding::sampledTexture( 1 + i, QRhiShaderResourceBinding::FragmentStage, pTexture, m_pSampler.get() ) );
		}
		bindings.push_back( QRhiShaderResourceBinding::sampledTexture( 5, QRhiShaderResourceBinding::FragmentStage, m_pScene.get(), m_pSampler.get() ) );
		m_pBindings.reset( m_pRhi->newShaderResourceBindings() );
		m_pBindings->setBindings( bindings.begin(), bindings.end() );
		m_pBindings->create();
		m_pBoundScene = m_pScene.get();
		m_pPipeline.reset();
	}
	if ( !m_pPipeline || m_pPipelinePass != pFinal->renderPassDescriptor() )
	{
		m_pPipeline.reset( m_pRhi->newGraphicsPipeline() );
		m_pPipeline->setShaderStages( { { QRhiShaderStage::Vertex, LoadShader( QStringLiteral( ":/shaders/fullscreen.vert.qsb" ) ) }, { QRhiShaderStage::Fragment, shader } } );
		m_pPipeline->setVertexInputLayout( {} );
		m_pPipeline->setShaderResourceBindings( m_pBindings.get() );
		m_pPipeline->setRenderPassDescriptor( pFinal->renderPassDescriptor() );
		m_pPipeline->setSampleCount( pFinal->sampleCount() );
		if ( !m_pPipeline->create() )
		{
			m_pPipeline.reset();
			pUpdates->release();
			return false;
		}
		m_pPipelinePass = pFinal->renderPassDescriptor();
	}
	m_pFor = pEffect;

	// The constants: the program's own def values over the material's (as the card's do), then
	// what the material sets in c0 to c7.
	auto uniforms = std::make_unique<EffectUniforms_t>();
	std::memset( uniforms.get(), 0, sizeof( EffectUniforms_t ) );
	const xv::shader::ScreenEffect_t effect = pEffect->At( flTime );
	const auto set = [ & ]( int n, const xv::shader::Vec4_t &value ) {
		if ( n >= 0 && n < xv::shader::GLSL_CONSTANTS )
			std::memcpy( uniforms->m_Constants[ n ], value.data(), sizeof( float ) * 4 );
	};
	for ( int c = 0; c < 4; c++ )
		set( c, effect.m_Constants[ static_cast<std::size_t>( c ) ] );
	for ( int i = 0; i < 4; i++ )
	{
		const QSize size = pEffect->IsFrameSampler( i ) ? m_Size : pEffect->GetTexture( i ).Empty() ? QSize( 1, 1 )
																									: QSize( pEffect->GetTexture( i ).m_nWidth, pEffect->GetTexture( i ).m_nHeight );
		set( 4 + i, { 1.0f / static_cast<float>( std::max( 1, size.width() ) ), 1.0f / static_cast<float>( std::max( 1, size.height() ) ), 0.0f, 0.0f } );
		uniforms->m_FrameSampler[ i ] = pEffect->IsFrameSampler( i ) ? 1.0f : 0.0f;
		uniforms->m_Srgb[ i ] = effect.m_LinearRead[ static_cast<std::size_t>( i ) ] ? 0.0f : 1.0f;
	}
	if ( const xv::shader::CPixelShader *pShader = pEffect->GetShader() )
	{
		for ( const xv::shader::Instruction_t &instruction : pShader->GetProgram().m_Instructions )
		{
			if ( !instruction.m_Dest )
				continue;
			const std::uint32_t n = instruction.m_Dest->m_nIndex;
			if ( instruction.m_nOpcode == xv::shader::Opcode_t::OPCODE_DEF )
				set( static_cast<int>( n ), { std::bit_cast<float>( instruction.m_Values[ 0 ] ), std::bit_cast<float>( instruction.m_Values[ 1 ] ), std::bit_cast<float>( instruction.m_Values[ 2 ] ), std::bit_cast<float>( instruction.m_Values[ 3 ] ) } );
			else if ( instruction.m_nOpcode == xv::shader::Opcode_t::OPCODE_DEFI && n < 16 )
				for ( std::size_t k = 0; k < 4; k++ )
					uniforms->m_Integers[ n ][ k ] = static_cast<std::int32_t>( instruction.m_Values[ k ] );
			else if ( instruction.m_nOpcode == xv::shader::Opcode_t::OPCODE_DEFB && n < 16 )
				uniforms->m_Bools[ n ][ 0 ] = instruction.m_Values[ 0 ] ? 1.0f : 0.0f;
		}
	}
	const float flYUp = m_pRhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
	const QSize size = pFinal->pixelSize();
	uniforms->m_Screen[ 0 ] = static_cast<float>( size.width() );
	uniforms->m_Screen[ 1 ] = static_cast<float>( size.height() );
	uniforms->m_Screen[ 2 ] = flYUp;
	uniforms->m_Screen[ 3 ] = flYUp;
	uniforms->m_Mode[ 0 ] = static_cast<float>( static_cast<int>( effect.m_nBlend ) );
	uniforms->m_Mode[ 1 ] = effect.m_bLinearWrite ? 1.0f : 0.0f;
	uniforms->m_Mode[ 2 ] = effect.m_bCopyAlpha ? 1.0f : 0.0f;
	pUpdates->updateDynamicBuffer( m_pUniforms.get(), 0, sizeof( EffectUniforms_t ), uniforms.get() );

	pCommands->beginPass( pFinal, Qt::black, { 1.0f, 0 }, pUpdates );
	pCommands->setGraphicsPipeline( m_pPipeline.get() );
	pCommands->setViewport( { 0, 0, static_cast<float>( size.width() ), static_cast<float>( size.height() ) } );
	pCommands->setShaderResources( m_pBindings.get() );
	pCommands->draw( 3 );
	pCommands->endPass();
	return true;
}

#endif
