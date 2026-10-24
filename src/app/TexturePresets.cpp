//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See TexturePresets.h.
//
//===========================================================================//
#include "TexturePresets.h"

#include <QCoreApplication>
#include <QSettings>
#include <QVariantMap>

using namespace xv::texture;

namespace
{

const char *const PRESETS_KEY = "textures/presets";
const char *const LAST_PRESET_KEY = "textures/lastPreset";

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Tr( const char *pszText )
{
	return QCoreApplication::translate( "TexturePresets", pszText );
}

//-----------------------------------------------------------------------------
// Purpose: A preset's choices as settings keep them.
//-----------------------------------------------------------------------------
QVariantMap ToMap( const QString &szName, const BuildOptions_t &options )
{
	return {
		{ QStringLiteral( "name" ), szName },
		{ QStringLiteral( "format" ), static_cast<int>( options.m_Write.m_nFormat ) },
		{ QStringLiteral( "alphaFormat" ), static_cast<int>( options.m_nAlphaFormat ) },
		{ QStringLiteral( "flags" ), static_cast<qulonglong>( options.m_Write.m_nFlags ) },
		{ QStringLiteral( "mipmaps" ), options.m_Write.m_bMipmaps },
		{ QStringLiteral( "mipFilter" ), static_cast<int>( options.m_Write.m_nMipFilter ) },
		{ QStringLiteral( "thumbnail" ), options.m_Write.m_bLowRes },
		{ QStringLiteral( "version" ), options.m_Write.m_nVersionMinor },
		{ QStringLiteral( "resize" ), static_cast<int>( options.m_nResize ) },
		{ QStringLiteral( "maxSize" ), options.m_nMaxSize },
		{ QStringLiteral( "keepAspect" ), options.m_bKeepAspect },
		{ QStringLiteral( "alphaFlags" ), options.m_bAlphaFlags },
		{ QStringLiteral( "stripAlpha" ), options.m_bStripAlpha },
		{ QStringLiteral( "heightToNormal" ), options.m_bHeightToNormal },
		{ QStringLiteral( "normalScale" ), static_cast<double>( options.m_flNormalScale ) },
		{ QStringLiteral( "greenUp" ), options.m_bGreenUp },
		{ QStringLiteral( "distanceAlpha" ), options.m_bDistanceAlpha },
		{ QStringLiteral( "distanceSpread" ), static_cast<double>( options.m_flDistanceSpread ) },
	};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
BuildOptions_t FromMap( const QVariantMap &map )
{
	BuildOptions_t options;
	options.m_Write.m_nFormat = static_cast<VtfFormat_t>( map.value( QStringLiteral( "format" ), static_cast<int>( VtfFormat_t::DXT1 ) ).toInt() );
	options.m_nAlphaFormat = static_cast<VtfFormat_t>( map.value( QStringLiteral( "alphaFormat" ), static_cast<int>( VtfFormat_t::VTF_FORMAT_NONE ) ).toInt() );
	options.m_Write.m_nFlags = static_cast<std::uint32_t>( map.value( QStringLiteral( "flags" ) ).toULongLong() );
	options.m_Write.m_bMipmaps = map.value( QStringLiteral( "mipmaps" ), true ).toBool();
	options.m_Write.m_nMipFilter = static_cast<MipFilter_t>( map.value( QStringLiteral( "mipFilter" ), static_cast<int>( MipFilter_t::MIP_FILTER_KAISER ) ).toInt() );
	options.m_Write.m_bLowRes = map.value( QStringLiteral( "thumbnail" ), true ).toBool();
	options.m_Write.m_nVersionMinor = map.value( QStringLiteral( "version" ), 2 ).toInt();
	options.m_nResize = static_cast<ResizeMode_t>( map.value( QStringLiteral( "resize" ), static_cast<int>( ResizeMode_t::RESIZE_NEAREST ) ).toInt() );
	options.m_nMaxSize = map.value( QStringLiteral( "maxSize" ) ).toInt();
	options.m_bKeepAspect = map.value( QStringLiteral( "keepAspect" ) ).toBool();
	options.m_bAlphaFlags = map.value( QStringLiteral( "alphaFlags" ), true ).toBool();
	options.m_bStripAlpha = map.value( QStringLiteral( "stripAlpha" ) ).toBool();
	options.m_bHeightToNormal = map.value( QStringLiteral( "heightToNormal" ) ).toBool();
	options.m_flNormalScale = static_cast<float>( map.value( QStringLiteral( "normalScale" ), 2.0 ).toDouble() );
	options.m_bGreenUp = map.value( QStringLiteral( "greenUp" ) ).toBool();
	options.m_bDistanceAlpha = map.value( QStringLiteral( "distanceAlpha" ) ).toBool();
	options.m_flDistanceSpread = static_cast<float>( map.value( QStringLiteral( "distanceSpread" ), 4.0 ).toDouble() );
	return options;
}

//-----------------------------------------------------------------------------
// Purpose: The presets that come with the app, set the way Valve's own textures of each kind are.
//-----------------------------------------------------------------------------
std::vector<TexturePreset_t> Builtins()
{
	const auto make = []( const QString &szName, VtfFormat_t nFormat, VtfFormat_t nAlpha, std::uint32_t nFlags, bool bMipmaps, int nVersion, ResizeMode_t nResize ) {
		TexturePreset_t preset{ szName, {}, true };
		preset.m_Options.m_Write.m_nFormat = nFormat;
		preset.m_Options.m_nAlphaFormat = nAlpha;
		preset.m_Options.m_Write.m_nFlags = nFlags;
		preset.m_Options.m_Write.m_bMipmaps = bMipmaps;
		preset.m_Options.m_Write.m_nMipFilter = MipFilter_t::MIP_FILTER_KAISER;
		preset.m_Options.m_Write.m_nVersionMinor = nVersion;
		preset.m_Options.m_nResize = nResize;
		return preset;
	};
	using namespace vtf_flag;
	std::vector<TexturePreset_t> presets{
		make( Tr( "Diffuse (colour)" ), VtfFormat_t::DXT1, VtfFormat_t::DXT5, 0, true, 2, ResizeMode_t::RESIZE_NEAREST ),
		make( Tr( "Normal map" ), VtfFormat_t::DXT5, VtfFormat_t::VTF_FORMAT_NONE, NORMAL, true, 2, ResizeMode_t::RESIZE_NEAREST ),
		make( Tr( "Normal map from a height map" ), VtfFormat_t::DXT5, VtfFormat_t::VTF_FORMAT_NONE, NORMAL, true, 2, ResizeMode_t::RESIZE_NEAREST ),
		make( Tr( "Decal" ), VtfFormat_t::DXT5, VtfFormat_t::VTF_FORMAT_NONE, CLAMPS | CLAMPT, true, 2, ResizeMode_t::RESIZE_NEAREST ),
		make( Tr( "Particle sprite or sheet" ), VtfFormat_t::DXT5, VtfFormat_t::VTF_FORMAT_NONE, CLAMPS | CLAMPT, true, 4, ResizeMode_t::RESIZE_NEAREST ),
		make( Tr( "UI / HUD picture" ), VtfFormat_t::BGRA8888, VtfFormat_t::VTF_FORMAT_NONE, NOMIP | NOLOD | CLAMPS | CLAMPT, false, 2, ResizeMode_t::RESIZE_KEEP ),
		make( Tr( "Sky face (CS:S, LDR)" ), VtfFormat_t::DXT5, VtfFormat_t::VTF_FORMAT_NONE, CLAMPS | CLAMPT | HINT_DXT5, true, 1, ResizeMode_t::RESIZE_NEAREST ),
		make( Tr( "Sky face (HDR)" ), VtfFormat_t::RGBA16161616F, VtfFormat_t::VTF_FORMAT_NONE, 0, false, 2, ResizeMode_t::RESIZE_NEAREST ),
		make( Tr( "Distance-field sign or icon ($distancealpha)" ), VtfFormat_t::DXT5, VtfFormat_t::VTF_FORMAT_NONE, 0, true, 2, ResizeMode_t::RESIZE_NEAREST ),
	};
	presets[ 2 ].m_Options.m_bHeightToNormal = true;
	presets[ 8 ].m_Options.m_bDistanceAlpha = true;
	presets[ 8 ].m_Options.m_nMaxSize = 512;
	return presets;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The built-in presets, then the user's.
//-----------------------------------------------------------------------------
std::vector<TexturePreset_t> TexturePresets()
{
	std::vector<TexturePreset_t> presets = Builtins();
	for ( const QVariant &value : QSettings{}.value( QString::fromLatin1( PRESETS_KEY ) ).toList() )
	{
		const QVariantMap map = value.toMap();
		presets.push_back( { map.value( QStringLiteral( "name" ) ).toString(), FromMap( map ), false } );
	}
	return presets;
}

//-----------------------------------------------------------------------------
// Purpose: Saves (or replaces, by name) one of the user's presets.
//-----------------------------------------------------------------------------
bool SaveTexturePreset( const QString &szName, const BuildOptions_t &options )
{
	if ( szName.trimmed().isEmpty() || std::ranges::any_of( Builtins(), [ &szName ]( const TexturePreset_t &preset ) { return preset.m_szName == szName; } ) )
	{
		return false;
	}
	QSettings settings;
	QVariantList list = settings.value( QString::fromLatin1( PRESETS_KEY ) ).toList();
	list.removeIf( [ &szName ]( const QVariant &value ) { return value.toMap().value( QStringLiteral( "name" ) ).toString() == szName; } );
	list.append( ToMap( szName.trimmed(), options ) );
	settings.setValue( QString::fromLatin1( PRESETS_KEY ), list );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void DeleteTexturePreset( const QString &szName )
{
	QSettings settings;
	QVariantList list = settings.value( QString::fromLatin1( PRESETS_KEY ) ).toList();
	list.removeIf( [ &szName ]( const QVariant &value ) { return value.toMap().value( QStringLiteral( "name" ) ).toString() == szName; } );
	settings.setValue( QString::fromLatin1( PRESETS_KEY ), list );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString LastTexturePreset()
{
	return QSettings{}.value( QString::fromLatin1( LAST_PRESET_KEY ) ).toString();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetLastTexturePreset( const QString &szName )
{
	QSettings{}.setValue( QString::fromLatin1( LAST_PRESET_KEY ), szName );
}
