//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Sky.h.
//
//===========================================================================//
#include "Sky.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <thread>

#include "kv1/Kv1.h"
#include "material/Material.h"
#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace xv::sky
{

namespace
{

using texture::CImage;

// Which way each face looks and how its image is turned. Established from the game's own skies,
// not from engine code:
// - Placement relative to each other: in all 63 skies CS:S ships (its own and HL2's), the pixels on
//   either side of the cube's edges agree (a mean difference of at most about 2 out of 255 between
//   the sides), and turning or mirroring any face makes its edges match clearly worse. Stock down
//   faces are plain at their edges, so their turn follows what all the others show: no face is
//   mirrored seen from inside, and up's bottom edge and dn's top edge both meet rt.
// - Absolute direction: the sun drawn in stock skies sits where the map's light_environment puts it
//   (de_aztec, de_cbble, de_piranesi, cs_havana and cs_italy within 6 degrees, de_dust2 and
//   de_chateau within 18), which puts rt at +X. A turned or mirrored cube would put those suns tens
//   of degrees off.
// The corpus tests repeat these checks (seams, up and down turns, suns) against the game files.
constexpr std::array<FaceBasis_t, 6> BASES{ {
	{ { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } },  // rt: east, yaw 0
	{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },   // bk: north, yaw 90
	{ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },  // lf: west, yaw 180
	{ { 0, -1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } }, // ft: south, yaw 270
	{ { 0, 0, 1 }, { 0, -1, 0 }, { -1, 0, 0 } }, // up: its top edge toward lf
	{ { 0, 0, -1 }, { 0, -1, 0 }, { 1, 0, 0 } }, // dn: its top edge toward rt
} };

constexpr std::array<std::string_view, 6> SUFFIXES{ "rt", "bk", "lf", "ft", "up", "dn" };

constexpr float DEGREES = 180.0f / std::numbers::pi_v<float>;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut{ szText };
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 3> Bilinear( const CImage &image, float u, float v, bool bRepeatU = false, bool bRepeatV = false )
{
	// Clamped: the texels at the edge carry on. Repeated: the far side's texels come round.
	const auto axis = []( float t, int nSize, bool bRepeat, int &n0, int &n1, float &flWeight ) {
		const float f = t * static_cast<float>( nSize ) - 0.5f;
		if ( bRepeat )
		{
			const float flFloor = std::floor( f );
			flWeight = f - flFloor;
			const long long nWhole = static_cast<long long>( flFloor );
			n0 = static_cast<int>( ( ( nWhole % nSize ) + nSize ) % nSize );
			n1 = ( n0 + 1 ) % nSize;
			return;
		}
		const float flClamped = std::clamp( f, 0.0f, static_cast<float>( nSize - 1 ) );
		n0 = static_cast<int>( flClamped );
		n1 = std::min( n0 + 1, nSize - 1 );
		flWeight = flClamped - static_cast<float>( n0 );
	};
	int nX0 = 0, nX1 = 0, nY0 = 0, nY1 = 0;
	float flWx = 0.0f, flWy = 0.0f;
	axis( u, image.m_nWidth, bRepeatU, nX0, nX1, flWx );
	axis( v, image.m_nHeight, bRepeatV, nY0, nY1, flWy );
	const std::uint8_t *pP00 = image.Pixel( nX0, nY0 );
	const std::uint8_t *pP10 = image.Pixel( nX1, nY0 );
	const std::uint8_t *pP01 = image.Pixel( nX0, nY1 );
	const std::uint8_t *pP11 = image.Pixel( nX1, nY1 );
	std::array<float, 3> vecOut{};
	for ( std::size_t c = 0; c < 3; c++ )
	{
		const float flTop = static_cast<float>( pP00[ c ] ) + ( static_cast<float>( pP10[ c ] ) - static_cast<float>( pP00[ c ] ) ) * flWx;
		const float flBottom = static_cast<float>( pP01[ c ] ) + ( static_cast<float>( pP11[ c ] ) - static_cast<float>( pP01[ c ] ) ) * flWx;
		vecOut[ c ] = flTop + ( flBottom - flTop ) * flWy;
	}
	return vecOut;
}

//-----------------------------------------------------------------------------
// Purpose: Samples the cube with one face's image swapped for another (for trying out
//          orientations).
//-----------------------------------------------------------------------------
std::array<float, 3> SampleWith( const CCube &cube, CVec3 vecDir, Face_t nSwapped, const CImage *pReplacement )
{
	const FacePoint_t point = Project( vecDir );
	const CImage &image = pReplacement && point.m_nFace == nSwapped ? *pReplacement : cube.Face( point.m_nFace );
	if ( image.Empty() )
	{
		return {};
	}
	const CUvTransform &t = cube.m_Transforms[ IndexOf( point.m_nFace ) ];
	return Bilinear( image, t.m_flA * point.m_flU + t.m_flB * point.m_flV + t.m_flTx, t.m_flC * point.m_flU + t.m_flD * point.m_flV + t.m_flTy, t.m_bRepeatU, t.m_bRepeatV );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float EdgeDifference( const CCube &cube, Face_t a, Face_t b, Face_t nSwapped, const CImage *pReplacement )
{
	constexpr int nSAMPLES = 128;
	constexpr float flINSET = 0.004f; // about a pixel of a 512 face into each side
	const CVec3 vecNa = Basis( a ).m_vecNormal;
	const CVec3 vecNb = Basis( b ).m_vecNormal;
	const CVec3 vecAlong = Cross( vecNa, vecNb );
	float flTotal = 0.0f;
	for ( int i = 0; i < nSAMPLES; i++ )
	{
		const float t = -1.0f + ( 2.0f * static_cast<float>( i ) + 1.0f ) / nSAMPLES;
		const CVec3 vecPoint = vecNa + vecNb + vecAlong * t;
		const auto vecCa = SampleWith( cube, vecPoint - vecNb * flINSET, nSwapped, pReplacement );
		const auto vecCb = SampleWith( cube, vecPoint - vecNa * flINSET, nSwapped, pReplacement );
		flTotal += ( std::abs( vecCa[ 0 ] - vecCb[ 0 ] ) + std::abs( vecCa[ 1 ] - vecCb[ 1 ] ) + std::abs( vecCa[ 2 ] - vecCb[ 2 ] ) ) / 3.0f;
	}
	return flTotal / nSAMPLES;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float FaceDifference( const CCube &cube, Face_t nFace, const CImage *pReplacement )
{
	float flTotal = 0.0f;
	int nCount = 0;
	for ( Face_t nOther : FACES )
	{
		if ( std::abs( Dot( Basis( nFace ).m_vecNormal, Basis( nOther ).m_vecNormal ) ) < 0.5f && !cube.Face( nOther ).Empty() )
		{
			flTotal += EdgeDifference( cube, nFace, nOther, nFace, pReplacement );
			nCount++;
		}
	}
	return nCount ? flTotal / static_cast<float>( nCount ) : 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Runs body(begin, end) over rows split between the machine's cores. (std::thread rather
//          than
//          jthread: libc++ only has jthread from version 18.)
//-----------------------------------------------------------------------------
template <typename Body>
void ParallelRows( int nRows, Body &&body )
{
	const int nThreads = std::clamp( static_cast<int>( std::thread::hardware_concurrency() ), 1, 32 );
	if ( nThreads == 1 || nRows < 64 )
	{
		body( 0, nRows );
		return;
	}
	std::vector<std::thread> workers;
	const int nChunk = ( nRows + nThreads - 1 ) / nThreads;
	for ( int nBegin = 0; nBegin < nRows; nBegin += nChunk )
	{
		workers.emplace_back( [ &body, nBegin, nEnd = std::min( nRows, nBegin + nChunk ) ] { body( nBegin, nEnd ); } );
	}
	for ( std::thread &worker : workers )
	{
		worker.join();
	}
}

using Reader_t = std::function<std::optional<std::vector<std::byte>>( const std::string &szGamePath )>;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> ReadFile( const std::filesystem::path &path )
{
	std::ifstream stream{ path, std::ios::binary };
	if ( !stream )
	{
		return std::nullopt;
	}
	std::vector<char> chars{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
	std::vector<std::byte> bytes( chars.size() );
	std::memcpy( bytes.data(), chars.data(), chars.size() );
	return bytes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CImage> DecodeTexture( std::span<const std::byte> bytes, std::string &szError )
{
	auto vtf = texture::ReadVtf( bytes, &szError );
	return vtf ? vtf->Decode( 0, 0, 0, &szError ) : std::nullopt;
}

// Cube maps read once per sky, by game path: one can be a hundred megabytes.
using CubeMaps_t = std::map<std::string, std::optional<texture::CVtf>>;

//-----------------------------------------------------------------------------
// Purpose: One face from its material: its texture transform, and the first of its textures that
//          reads
//          (the LDR one first; the HDR one when that's missing, as the game would show with HDR
//          on).
//          Returns what went wrong if none does.
//-----------------------------------------------------------------------------
std::optional<std::string> LoadMaterialFace( LoadedSky_t &sky, Face_t nFace, const std::string &szMaterialPath, std::string_view szText, const Reader_t &pfnRead, CubeMaps_t &cubeMaps )
{
	const kv1::CDocument document = kv1::Parse( szText ).m_Document;
	const auto root = std::ranges::find_if( document.m_Roots, &kv1::CNode::IsBlock );
	if ( root == document.m_Roots.end() )
	{
		return szMaterialPath + " has no shader block";
	}
	// The texture transform as it is when the map starts: some skies set it with proxies, picking
	// their part of a texture shared by several faces (TextureTransform over an atlas).
	const material::Reader_t pfnMaterialRead = [ &pfnRead ]( std::string_view szPath ) { return pfnRead( std::string{ szPath } ); };
	if ( const auto material = material::ParseMaterial( szText, pfnMaterialRead ) )
	{
		const material::CMaterial start = material::CProxies( *material ).At( 0.0f, {} );
		if ( const material::Parameter_t *pTransform = start.Find( "$basetexturetransform" ) )
		{
			sky.m_Cube.m_Transforms[ IndexOf( nFace ) ] = CUvTransform::Parse( pTransform->m_szValue );
		}
	}
	std::optional<std::string> firstProblem;
	for ( std::string_view szKey : { "$basetexture", "$hdrbasetexture", "$hdrcompressedtexture" } )
	{
		const kv1::CNode *pTexture = root->Find( szKey );
		if ( !pTexture || !pTexture->IsPair() )
		{
			continue;
		}
		const std::string szVtfPath = vfs::NormalizePath( "materials/" + pTexture->GetValue() + ".vtf" );
		const auto bytes = pfnRead( szVtfPath );
		std::string szError = "missing";
		const auto vtf = bytes ? texture::ReadVtf( *bytes, &szError ) : std::nullopt;
		auto image = vtf ? vtf->Decode( 0, 0, 0, &szError ) : std::nullopt;
		if ( !image )
		{
			firstProblem = firstProblem.value_or( szVtfPath + ": " + szError );
			continue;
		}
		// A transformed face can reach past the texture's edges: it repeats there unless the
		// texture is clamped, as sky textures usually are.
		if ( CUvTransform &transform = sky.m_Cube.m_Transforms[ IndexOf( nFace ) ]; !transform.IsIdentity() )
		{
			transform.m_bRepeatU = ( vtf->m_Info.m_nFlags & texture::vtf_flag::CLAMPS ) == 0;
			transform.m_bRepeatV = ( vtf->m_Info.m_nFlags & texture::vtf_flag::CLAMPT ) == 0;
		}
		sky.m_Cube.Face( nFace ) = std::move( *image );
		sky.m_Textures[ IndexOf( nFace ) ] = szVtfPath;
		return std::nullopt;
	}
	// A sky drawn from a cube map (WindowImposter and the like): its face for this side.
	if ( const kv1::CNode *pEnvmap = root->Find( "$envmap" ); !firstProblem && pEnvmap && pEnvmap->IsPair() )
	{
		if ( Lower( pEnvmap->GetValue() ) == "env_cubemap" )
		{
			return szMaterialPath + " reflects the map's nearest cube map (env_cubemap), which only the map has";
		}
		const std::string szVtfPath = vfs::NormalizePath( "materials/" + pEnvmap->GetValue() + ".vtf" );
		auto [ it, bNew ] = cubeMaps.try_emplace( szVtfPath );
		std::string szError = "missing";
		if ( bNew )
		{
			if ( const auto bytes = pfnRead( szVtfPath ) )
			{
				it->second = texture::ReadVtf( *bytes, &szError );
			}
		}
		if ( !it->second )
		{
			return szVtfPath + ": " + szError;
		}
		if ( !IsCubeMap( *it->second ) )
		{
			return szVtfPath + " is not a cube map";
		}
		auto image = CubeMapFace( *it->second, nFace, &szError );
		if ( !image )
		{
			return szVtfPath + ": " + szError;
		}
		sky.m_Cube.Face( nFace ) = std::move( *image );
		sky.m_Cube.m_Transforms[ IndexOf( nFace ) ] = {}; // the cube map's faces are whole squares
		sky.m_Textures[ IndexOf( nFace ) ] = szVtfPath;
		return std::nullopt;
	}
	return firstProblem.value_or( szMaterialPath + " names no texture" );
}

//-----------------------------------------------------------------------------
// Purpose: The folder called materials that path is in, if any.
//-----------------------------------------------------------------------------
std::filesystem::path MaterialsFolder( const std::filesystem::path &path )
{
	for ( std::filesystem::path dir = path.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path() )
	{
		if ( Lower( vfs::ToUtf8( dir.filename() ) ) == "materials" )
		{
			return dir;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: A face as a plain square image, with its texture transform (if any) applied.
//-----------------------------------------------------------------------------
CImage BakedFace( const CCube &cube, Face_t nFace )
{
	const CImage &image = cube.Face( nFace );
	const CUvTransform &t = cube.m_Transforms[ IndexOf( nFace ) ];
	if ( t.IsIdentity() )
	{
		return image;
	}
	const int nSize = std::max( image.m_nWidth, image.m_nHeight );
	CImage out{ nSize, nSize };
	for ( int y = 0; y < nSize; y++ )
	{
		for ( int x = 0; x < nSize; x++ )
		{
			const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( nSize );
			const float v = ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( nSize );
			const auto vecColour = Bilinear( image, t.m_flA * u + t.m_flB * v + t.m_flTx, t.m_flC * u + t.m_flD * v + t.m_flTy, t.m_bRepeatU, t.m_bRepeatV );
			std::uint8_t *p = out.Pixel( x, y );
			for ( std::size_t c = 0; c < 3; c++ )
			{
				p[ c ] = static_cast<std::uint8_t>( std::lround( vecColour[ c ] ) );
			}
			p[ 3 ] = 255;
		}
	}
	return out;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view Suffix( Face_t nFace )
{
	return SUFFIXES[ IndexOf( nFace ) ];
}

//-----------------------------------------------------------------------------
// Purpose: The face a file is for, from the suffix just before its extension: "mysky_rt.tga",
//          "skyUP.png".
//-----------------------------------------------------------------------------
std::optional<Face_t> FaceFromFileName( std::string_view szFileName )
{
	std::string szStem = Lower( szFileName.substr( 0, std::min( szFileName.size(), szFileName.rfind( '.' ) ) ) );
	for ( Face_t nFace : FACES )
	{
		if ( szStem.ends_with( Suffix( nFace ) ) )
		{
			return nFace;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const FaceBasis_t &Basis( Face_t nFace )
{
	return BASES[ IndexOf( nFace ) ];
}

//-----------------------------------------------------------------------------
// Purpose: The face on the other side of an edge of this face's image.
//-----------------------------------------------------------------------------
Face_t Neighbor( Face_t nFace, Edge_t nEdge )
{
	const FaceBasis_t &b = Basis( nFace );
	const CVec3 vecToward = nEdge == Edge_t::EDGE_TOP ? b.m_vecUp : nEdge == Edge_t::EDGE_BOTTOM ? -b.m_vecUp
		: nEdge == Edge_t::EDGE_RIGHT                                                            ? b.m_vecRight
																								 : -b.m_vecRight;
	for ( Face_t nOther : FACES )
	{
		if ( Basis( nOther ).m_vecNormal == vecToward )
		{
			return nOther;
		}
	}
	return nFace;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
FacePoint_t Project( CVec3 d )
{
	const float flAx = std::abs( d.m_flX );
	const float flAy = std::abs( d.m_flY );
	const float flAz = std::abs( d.m_flZ );
	Face_t nFace = Face_t::FACE_RT;
	float flMajor = flAx;
	if ( flAx >= flAy && flAx >= flAz )
	{
		nFace = d.m_flX >= 0.0f ? Face_t::FACE_RT : Face_t::FACE_LF;
	}
	else if ( flAy >= flAz )
	{
		nFace = d.m_flY >= 0.0f ? Face_t::FACE_BK : Face_t::FACE_FT;
		flMajor = flAy;
	}
	else
	{
		nFace = d.m_flZ >= 0.0f ? Face_t::FACE_UP : Face_t::FACE_DN;
		flMajor = flAz;
	}
	if ( flMajor <= 0.0f )
	{
		return { Face_t::FACE_RT, 0.5f, 0.5f };
	}
	const FaceBasis_t &b = Basis( nFace );
	const float s = Dot( d, b.m_vecRight ) / flMajor;
	const float t = Dot( d, b.m_vecUp ) / flMajor;
	return { nFace, ( s + 1.0f ) * 0.5f, ( 1.0f - t ) * 0.5f };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Direction( Face_t nFace, float u, float v )
{
	const FaceBasis_t &b = Basis( nFace );
	return b.m_vecNormal + b.m_vecRight * ( u * 2.0f - 1.0f ) + b.m_vecUp * ( 1.0f - v * 2.0f );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Forward( Angles_t angles )
{
	const float flPitch = angles.m_flPitch / DEGREES;
	const float flYaw = angles.m_flYaw / DEGREES;
	return { std::cos( flPitch ) * std::cos( flYaw ), std::cos( flPitch ) * std::sin( flYaw ), -std::sin( flPitch ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Angles_t AnglesOf( CVec3 d )
{
	d = Normalized( d );
	float flYaw = std::atan2( d.m_flY, d.m_flX ) * DEGREES;
	if ( flYaw < 0.0f )
	{
		flYaw += 360.0f;
	}
	return { -std::asin( std::clamp( d.m_flZ, -1.0f, 1.0f ) ) * DEGREES, flYaw };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CUvTransform::IsIdentity() const
{
	return m_flA == 1.0f && m_flB == 0.0f && m_flC == 0.0f && m_flD == 1.0f && m_flTx == 0.0f && m_flTy == 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: "center 0 0 scale 1 2 rotate 0 translate 0 0"; parts left out keep their defaults.
//-----------------------------------------------------------------------------
CUvTransform CUvTransform::Parse( std::string_view szText )
{
	float flCx = 0.5f; // Source's default centre
	float flCy = 0.5f;
	float flSx = 1.0f;
	float flSy = 1.0f;
	float flRotate = 0.0f;
	float flTx = 0.0f;
	float flTy = 0.0f;
	std::istringstream in{ std::string{ szText } };
	std::string szWord;
	const auto number = [ &in ]( float &flOut ) {
		std::string szToken;
		if ( in >> szToken )
		{
			if ( const auto value = ParseNumber<float>( szToken ) )
			{
				flOut = *value;
			}
		}
	};
	while ( in >> szWord )
	{
		szWord = Lower( szWord );
		if ( szWord == "center" )
		{
			number( flCx );
			number( flCy );
		}
		else if ( szWord == "scale" )
		{
			number( flSx );
			number( flSy );
		}
		else if ( szWord == "rotate" )
		{
			number( flRotate );
		}
		else if ( szWord == "translate" )
		{
			number( flTx );
			number( flTy );
		}
	}
	// uv' = centre + R * S * (uv - centre) + translate
	const float r = flRotate / DEGREES;
	const float flCosR = std::cos( r );
	const float flSinR = std::sin( r );
	CUvTransform t;
	t.m_flA = flCosR * flSx;
	t.m_flB = -flSinR * flSy;
	t.m_flC = flSinR * flSx;
	t.m_flD = flCosR * flSy;
	t.m_flTx = flCx - ( t.m_flA * flCx + t.m_flB * flCy ) + flTx;
	t.m_flTy = flCy - ( t.m_flC * flCx + t.m_flD * flCy ) + flTy;
	return t;
}

//-----------------------------------------------------------------------------
// Purpose: The colour seen in a direction, 0-255 per channel: bilinear, clamped at the face edges.
//-----------------------------------------------------------------------------
std::array<float, 3> Sample( const CCube &cube, CVec3 vecDir )
{
	return SampleWith( cube, vecDir, Face_t::FACE_RT, nullptr );
}

//-----------------------------------------------------------------------------
// Purpose: The direction through a point of the image (pixel coordinates, 0,0 at the top-left
//          corner).
//-----------------------------------------------------------------------------
CVec3 CView::Ray( float x, float y ) const
{
	const float flTanV = std::tan( m_flFov * 0.5f / DEGREES ) * 0.75f;
	const float flTanH = flTanV * static_cast<float>( m_nWidth ) / static_cast<float>( std::max( m_nHeight, 1 ) );
	const CVec3 f = Forward( m_Angles );
	const float flYaw = m_Angles.m_flYaw / DEGREES;
	const CVec3 vecRight{ std::sin( flYaw ), -std::cos( flYaw ), 0.0f };
	const CVec3 vecUp = Cross( vecRight, f );
	const float flSx = ( 2.0f * x / static_cast<float>( std::max( m_nWidth, 1 ) ) - 1.0f ) * flTanH;
	const float flSy = ( 1.0f - 2.0f * y / static_cast<float>( std::max( m_nHeight, 1 ) ) ) * flTanV;
	return f + vecRight * flSx + vecUp * flSy;
}

//-----------------------------------------------------------------------------
// Purpose: Where a direction appears in the image; nullopt when it is behind the viewer.
//-----------------------------------------------------------------------------
std::optional<std::array<float, 2>> CView::ToScreen( CVec3 vecDir ) const
{
	const float flTanV = std::tan( m_flFov * 0.5f / DEGREES ) * 0.75f;
	const float flTanH = flTanV * static_cast<float>( m_nWidth ) / static_cast<float>( std::max( m_nHeight, 1 ) );
	const CVec3 f = Forward( m_Angles );
	const float flYaw = m_Angles.m_flYaw / DEGREES;
	const CVec3 vecRight{ std::sin( flYaw ), -std::cos( flYaw ), 0.0f };
	const CVec3 vecUp = Cross( vecRight, f );
	const float flDepth = Dot( vecDir, f );
	if ( flDepth <= 1e-4f )
	{
		return std::nullopt;
	}
	const float flSx = Dot( vecDir, vecRight ) / flDepth / flTanH;
	const float flSy = Dot( vecDir, vecUp ) / flDepth / flTanV;
	return std::array<float, 2>{ ( flSx + 1.0f ) * 0.5f * static_cast<float>( m_nWidth ), ( 1.0f - flSy ) * 0.5f * static_cast<float>( m_nHeight ) };
}

//-----------------------------------------------------------------------------
// Purpose: Renders what a player sees, into out (view.width x view.height). edges outlines the
//          faces.
//-----------------------------------------------------------------------------
void Render( const CCube &cube, const CView &view, CImage &out, bool bEdges )
{
	if ( out.m_nWidth != view.m_nWidth || out.m_nHeight != view.m_nHeight )
	{
		out = CImage{ view.m_nWidth, view.m_nHeight };
	}
	ParallelRows( view.m_nHeight, [ & ]( int nBegin, int nEnd ) {
		for ( int y = nBegin; y < nEnd; y++ )
		{
			for ( int x = 0; x < view.m_nWidth; x++ )
			{
				const CVec3 vecDir = view.Ray( static_cast<float>( x ) + 0.5f, static_cast<float>( y ) + 0.5f );
				auto vecColour = Sample( cube, vecDir );
				if ( bEdges )
				{
					// Within about a pixel of a face's border: draw the edge.
					const FacePoint_t here = Project( vecDir );
					const FacePoint_t next = Project( view.Ray( static_cast<float>( x ) + 1.5f, static_cast<float>( y ) + 0.5f ) );
					const FacePoint_t below = Project( view.Ray( static_cast<float>( x ) + 0.5f, static_cast<float>( y ) + 1.5f ) );
					if ( here.m_nFace != next.m_nFace || here.m_nFace != below.m_nFace )
					{
						vecColour = { 255.0f, 200.0f, 0.0f };
					}
				}
				std::uint8_t *p = out.Pixel( x, y );
				for ( std::size_t c = 0; c < 3; c++ )
				{
					p[ c ] = static_cast<std::uint8_t>( std::clamp( vecColour[ c ] + 0.5f, 0.0f, 255.0f ) );
				}
				p[ 3 ] = 255;
			}
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Seam_t> Seams( const CCube &cube )
{
	std::vector<Seam_t> out;
	for ( std::size_t i = 0; i < FACES.size(); i++ )
	{
		for ( std::size_t j = i + 1; j < FACES.size(); j++ )
		{
			const Face_t a = FACES[ i ];
			const Face_t b = FACES[ j ];
			if ( std::abs( Dot( Basis( a ).m_vecNormal, Basis( b ).m_vecNormal ) ) < 0.5f && !cube.Face( a ).Empty() && !cube.Face( b ).Empty() )
			{
				out.push_back( { a, b, EdgeDifference( cube, a, b, a, nullptr ) } );
			}
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float SeamDifference( const CCube &cube, Face_t nFace )
{
	return cube.Face( nFace ).Empty() ? 0.0f : FaceDifference( cube, nFace, nullptr );
}

//-----------------------------------------------------------------------------
// Purpose: Fixes for the up and down faces, which other tools often store turned. Only clear wins
//          are
//          suggested; faces with plain edges (most down faces) give none.
//-----------------------------------------------------------------------------
std::vector<OrientationFix_t> SuggestOrientation( const CCube &cube )
{
	std::vector<OrientationFix_t> fixes;
	for ( Face_t nFace : { Face_t::FACE_UP, Face_t::FACE_DN } )
	{
		const CImage &image = cube.Face( nFace );
		if ( image.Empty() )
		{
			continue;
		}
		const float flBefore = FaceDifference( cube, nFace, nullptr );
		OrientationFix_t best{ nFace, 0, false, flBefore, flBefore };
		for ( int nTurns = 0; nTurns < 4; nTurns++ )
		{
			for ( bool bMirror : { false, true } )
			{
				if ( nTurns == 0 && !bMirror )
				{
					continue;
				}
				const CImage candidate = texture::Oriented( image, nTurns, bMirror );
				const float flAfter = FaceDifference( cube, nFace, &candidate );
				if ( flAfter < best.m_flAfter )
				{
					best = { nFace, nTurns, bMirror, flBefore, flAfter };
				}
			}
		}
		// Only clear wins: noisy or plain edges make small differences meaningless.
		if ( best.m_flAfter < flBefore * 0.6f && flBefore - best.m_flAfter > 3.0f )
		{
			fixes.push_back( best );
		}
	}
	return fixes;
}

//-----------------------------------------------------------------------------
// Purpose: The centre of the brightest spot, which is the sun in most skies.
//-----------------------------------------------------------------------------
std::optional<CVec3> BrightestSpot( const CCube &cube )
{
	const auto luminance = []( const std::uint8_t *p ) {
		return 0.299f * static_cast<float>( p[ 0 ] ) + 0.587f * static_cast<float>( p[ 1 ] ) + 0.114f * static_cast<float>( p[ 2 ] );
	};
	std::optional<Face_t> brightest;
	float flPeak = -1.0f;
	for ( Face_t nFace : FACES )
	{
		const CImage &image = cube.Face( nFace );
		for ( int y = 0; y < image.m_nHeight; y++ )
		{
			for ( int x = 0; x < image.m_nWidth; x++ )
			{
				if ( const float l = luminance( image.Pixel( x, y ) ); l > flPeak + 1.0f )
				{
					flPeak = l;
					brightest = nFace;
				}
			}
		}
	}
	if ( !brightest || flPeak <= 0.0f )
	{
		return std::nullopt;
	}
	const CImage &image = cube.Face( *brightest );
	double flSumX = 0.0;
	double flSumY = 0.0;
	int nCount = 0;
	for ( int y = 0; y < image.m_nHeight; y++ )
	{
		for ( int x = 0; x < image.m_nWidth; x++ )
		{
			if ( luminance( image.Pixel( x, y ) ) >= flPeak * 0.98f )
			{
				flSumX += x + 0.5;
				flSumY += y + 0.5;
				nCount++;
			}
		}
	}
	// Texture coordinates back to face coordinates (undoing a $basetexturetransform).
	const CUvTransform &t = cube.m_Transforms[ IndexOf( *brightest ) ];
	const float flTu = static_cast<float>( flSumX / nCount / image.m_nWidth ) - t.m_flTx;
	const float flTv = static_cast<float>( flSumY / nCount / image.m_nHeight ) - t.m_flTy;
	const float flDet = t.m_flA * t.m_flD - t.m_flB * t.m_flC;
	const float u = std::abs( flDet ) > 1e-6f ? ( t.m_flD * flTu - t.m_flB * flTv ) / flDet : flTu;
	const float v = std::abs( flDet ) > 1e-6f ? ( t.m_flA * flTv - t.m_flC * flTu ) / flDet : flTv;
	return Normalized( Direction( *brightest, std::clamp( u, 0.0f, 1.0f ), std::clamp( v, 0.0f, 1.0f ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Sky names become material and file names: letters, digits, '_', '-' and '.'.
//-----------------------------------------------------------------------------
bool IsValidName( std::string_view szName )
{
	return !szName.empty() && szName.size() <= 64 && std::ranges::all_of( szName, []( char c ) {
		return ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '.';
	} );
}

//-----------------------------------------------------------------------------
// Purpose: CLAMPS | CLAMPT so edges don't bleed, NOLOD so low texture detail settings don't blur
//          the sky,
//          and NOMIP when there are no mipmaps.
//-----------------------------------------------------------------------------
std::uint32_t VtfFlags( bool bMipmaps )
{
	using namespace texture::vtf_flag;
	return CLAMPS | CLAMPT | NOLOD | ( bMipmaps ? 0 : NOMIP );
}

//-----------------------------------------------------------------------------
// Purpose: The VMT of one face of an LDR sky.
//-----------------------------------------------------------------------------
std::string Material( std::string_view szName, Face_t nFace )
{
	return std::string{ "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"skybox/" } + std::string{ szName } + std::string{ Suffix( nFace ) } + "\"\n\t\"$nofog\" \"1\"\n\t\"$ignorez\" \"1\"\n}\n";
}

//-----------------------------------------------------------------------------
// Purpose: The VTF and VMT of every face (12 files). Faces are encoded in parallel. Empty if a
//          face is missing.
//-----------------------------------------------------------------------------
std::vector<OutputFile_t> ExportSky( const CCube &cube, const ExportOptions_t &options )
{
	if ( !IsValidName( options.m_szName ) || std::ranges::any_of( cube.m_Faces, &CImage::Empty ) || !texture::CanEncode( options.m_nFormat ) )
	{
		return {};
	}
	std::array<std::vector<std::byte>, 6> textures;
	std::vector<std::thread> workers;
	for ( Face_t nFace : FACES )
	{
		workers.emplace_back( [ &cube, &options, &textures, nFace ] {
			CImage image = BakedFace( cube, nFace );
			if ( options.m_nSize > 0 )
			{
				image = texture::Resize( image, options.m_nSize, options.m_nSize );
			}
			textures[ IndexOf( nFace ) ] = texture::WriteVtf( image, { .m_nFormat = options.m_nFormat, .m_nFlags = VtfFlags( options.m_bMipmaps ), .m_bMipmaps = options.m_bMipmaps, .m_bLowRes = true, .m_nVersionMinor = 2 } );
		} );
	}
	for ( std::thread &worker : workers )
	{
		worker.join();
	}
	std::vector<OutputFile_t> files;
	for ( Face_t nFace : FACES )
	{
		const std::string szBase = "materials/skybox/" + options.m_szName + std::string{ Suffix( nFace ) };
		const std::string szVmt = Material( options.m_szName, nFace );
		files.push_back( { szBase + ".vtf", std::move( textures[ IndexOf( nFace ) ] ) } );
		files.push_back( { szBase + ".vmt", { reinterpret_cast<const std::byte *>( szVmt.data() ), reinterpret_cast<const std::byte *>( szVmt.data() + szVmt.size() ) } } );
	}
	return files;
}

//-----------------------------------------------------------------------------
// Purpose: Reading skies from a game
//          The sky names the game has materials for (every skybox/<name>rt.vmt), sorted.
//-----------------------------------------------------------------------------
std::vector<std::string> SkyNames( const vfs::CFileSystem &fileSystem )
{
	std::set<std::string> names;
	constexpr std::string_view szPREFIX = "materials/skybox/";
	for ( const std::string &szPath : fileSystem.Find( szPREFIX, ".vmt" ) )
	{
		const std::string_view szFile = std::string_view{ szPath }.substr( szPREFIX.size() );
		if ( szFile.ends_with( "rt.vmt" ) && szFile.size() > 6 )
		{
			names.emplace( szFile.substr( 0, szFile.size() - 6 ) );
		}
	}
	return { names.begin(), names.end() };
}

//-----------------------------------------------------------------------------
// Purpose: Uses each material's $basetexture (the LDR image), else its HDR texture. A face with no
//          material
//          falls back to a VTF of the same name.
//-----------------------------------------------------------------------------
LoadedSky_t LoadSky( const vfs::CFileSystem &fileSystem, std::string_view szName )
{
	LoadedSky_t sky;
	CubeMaps_t cubeMaps;
	const Reader_t pfnRead = [ &fileSystem ]( const std::string &szPath ) { return fileSystem.Read( szPath ); };
	for ( Face_t nFace : FACES )
	{
		const std::string szBase = "materials/skybox/" + Lower( szName ) + std::string{ Suffix( nFace ) };
		std::string szMaterialPath = szBase + ".vmt";
		auto text = fileSystem.ReadText( szMaterialPath );
		if ( !text )
		{
			// Maps may name a sky whose materials only exist as <name>_hdr<face>.
			const std::string szHdr = "materials/skybox/" + Lower( szName ) + "_hdr" + std::string{ Suffix( nFace ) } + ".vmt";
			if ( ( text = fileSystem.ReadText( szHdr ) ) )
			{
				szMaterialPath = szHdr;
			}
		}
		if ( !text )
		{
			// No material: a texture of the same name, as a sky whose VMTs aren't written yet has.
			std::string szError = "missing";
			const auto bytes = fileSystem.Read( szBase + ".vtf" );
			if ( auto image = bytes ? DecodeTexture( *bytes, szError ) : std::nullopt )
			{
				sky.m_Cube.Face( nFace ) = std::move( *image );
				sky.m_Textures[ IndexOf( nFace ) ] = szBase + ".vtf";
			}
			else
			{
				sky.m_Problems.push_back( szMaterialPath + " is missing" );
			}
			continue;
		}
		sky.m_Materials[ IndexOf( nFace ) ] = szMaterialPath;
		if ( auto problem = LoadMaterialFace( sky, nFace, szMaterialPath, *text, pfnRead, cubeMaps ) )
		{
			sky.m_Problems.push_back( std::move( *problem ) );
		}
	}
	return sky;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CSkyFiles::Count() const
{
	return static_cast<int>( std::ranges::count_if( m_Faces, []( const std::filesystem::path &face ) { return !face.empty(); } ) );
}

//-----------------------------------------------------------------------------
// Purpose: The sky a file belongs to, if its name ends in a face suffix: the files for the other
//          faces
//          beside it. For each face the same kind of file is preferred (a .vmt finds .vmts), then
//          materials,
//          VTFs and images. Material and VTF names are kept exactly (they are the sky's name in
//          game);
//          image names lose a trailing separator ("mysky_rt.png" is the sky mysky).
//-----------------------------------------------------------------------------
std::optional<CSkyFiles> FindSkyFiles( const std::filesystem::path &file )
{
	const std::string szExtension = Lower( vfs::ToUtf8( file.extension() ) );
	const std::string szKind = szExtension.empty() ? std::string{} : szExtension.substr( 1 );
	const bool bGameFile = szKind == "vmt" || szKind == "vtf";
	if ( !bGameFile && !texture::IsImageExtension( szKind ) )
	{
		return std::nullopt;
	}
	const std::string szStem = vfs::ToUtf8( file.stem() );
	const auto face = FaceFromFileName( szStem );
	if ( !face || szStem.size() <= 2 )
	{
		return std::nullopt;
	}
	const std::string szBase = Lower( szStem.substr( 0, szStem.size() - 2 ) );

	// The folder's files by lowercase name, as games match names without case.
	std::map<std::string, std::filesystem::path> entries;
	std::error_code ec;
	for ( const auto &entry : std::filesystem::directory_iterator( file.parent_path(), ec ) )
	{
		if ( entry.is_regular_file( ec ) )
		{
			entries.emplace( Lower( vfs::ToUtf8( entry.path().filename() ) ), entry.path() );
		}
	}
	std::vector<std::string> kinds{ szKind, "vmt", "vtf", "png", "tga", "jpg", "jpeg", "bmp", "psd", "hdr" };
	CSkyFiles files;
	for ( Face_t f : FACES )
	{
		for ( const std::string &k : kinds )
		{
			if ( const auto it = entries.find( szBase + std::string{ Suffix( f ) } + "." + k ); it != entries.end() )
			{
				files.m_Faces[ IndexOf( f ) ] = it->second;
				break;
			}
		}
	}
	files.m_bInSkyboxFolder = Lower( vfs::ToUtf8( file.parent_path().filename() ) ) == "skybox";
	files.m_szName = szBase;
	if ( !bGameFile )
	{
		while ( !files.m_szName.empty() && ( files.m_szName.back() == '_' || files.m_szName.back() == '-' || files.m_szName.back() == '.' || files.m_szName.back() == ' ' ) )
		{
			files.m_szName.pop_back();
		}
	}
	return files;
}

//-----------------------------------------------------------------------------
// Purpose: Loads each face: a .vmt through its $basetexture, looked up under the materials folder
//          the .vmt
//          is in, then in fileSystem (if given), then beside the .vmt; a .vtf or image directly.
//-----------------------------------------------------------------------------
LoadedSky_t LoadSkyFiles( const CSkyFiles &files, const vfs::CFileSystem *pFileSystem )
{
	LoadedSky_t sky;
	CubeMaps_t cubeMaps;
	for ( Face_t nFace : FACES )
	{
		const std::filesystem::path &path = files.m_Faces[ IndexOf( nFace ) ];
		if ( path.empty() )
		{
			sky.m_Problems.push_back( "no file for " + std::string{ Suffix( nFace ) } );
			continue;
		}
		const std::string szShown = vfs::ToUtf8( path );
		const auto bytes = ReadFile( path );
		if ( !bytes )
		{
			sky.m_Problems.push_back( szShown + " can't be read" );
			continue;
		}
		const std::string szExtension = Lower( vfs::ToUtf8( path.extension() ) );
		std::string szError;
		if ( szExtension == ".vmt" )
		{
			// Texture names are relative to a materials folder: the one this material is in,
			// else the game's.
			const std::filesystem::path materials = MaterialsFolder( path );
			const Reader_t pfnRead = [ &materials, pFileSystem ]( const std::string &szGamePath ) -> std::optional<std::vector<std::byte>> {
				if ( !materials.empty() )
				{
					if ( auto data = ReadFile( vfs::FindCaseInsensitive( materials.parent_path() / vfs::FromUtf8( szGamePath ) ) ) )
					{
						return data;
					}
				}
				return pFileSystem ? pFileSystem->Read( szGamePath ) : std::nullopt;
			};
			const std::string szText{ reinterpret_cast<const char *>( bytes->data() ), bytes->size() };
			sky.m_Materials[ IndexOf( nFace ) ] = szShown;
			if ( auto problem = LoadMaterialFace( sky, nFace, szShown, szText, pfnRead, cubeMaps ) )
			{
				// Its texture can't be found: a VTF beside the material will do.
				std::filesystem::path beside = path;
				beside.replace_extension( ".vtf" );
				const auto data = ReadFile( vfs::FindCaseInsensitive( beside ) );
				if ( auto image = data ? DecodeTexture( *data, szError ) : std::nullopt )
				{
					sky.m_Cube.Face( nFace ) = std::move( *image );
					sky.m_Textures[ IndexOf( nFace ) ] = vfs::ToUtf8( beside );
				}
				else
				{
					sky.m_Problems.push_back( std::move( *problem ) );
				}
			}
			continue;
		}
		auto image = szExtension == ".vtf" ? DecodeTexture( *bytes, szError ) : texture::LoadImage( *bytes, &szError );
		if ( !image )
		{
			sky.m_Problems.push_back( szShown + ": " + szError );
			continue;
		}
		sky.m_Cube.Face( nFace ) = std::move( *image );
		sky.m_Textures[ IndexOf( nFace ) ] = szShown;
	}
	return sky;
}

//-----------------------------------------------------------------------------
// Purpose: A VTF with the six faces of a cube.
//-----------------------------------------------------------------------------
bool IsCubeMap( const texture::CVtf &vtf )
{
	return vtf.m_Info.m_nFaces >= 6;
}

//-----------------------------------------------------------------------------
// Purpose: How a cube map's faces are stored.
//
//          The faces are stored in the order rt lf bk ft up dn (the names Valve's tools give the
//          six images a cube map is built from). How each is turned was found from the files: the
//          ones Valve builds repeat each edge's texels on both faces it joins, and under this
//          table all twelve seams of materials/editor/cubemap.vtf and shadertest/cubemap.vtf
//          meet exactly (under 0.6 of 255 at full size), the stock maps' cube maps closely, and a
//          user's WindowImposter sky stands upright all round. Quarter turns are clockwise, the
//          mirror (left to right) comes after the turn, as texture::Oriented does them.
//-----------------------------------------------------------------------------
namespace
{

// Where each sky face is stored in a cube map, and how far it is turned there (it is mirrored
// too), as the note above says.
struct Stored_t
{
	int m_nIndex;
	int m_nQuarterTurns;
};
constexpr std::array<Stored_t, 6> STORED{ {
	{ 0, 1 }, // rt
	{ 2, 2 }, // bk
	{ 1, 3 }, // lf
	{ 3, 0 }, // ft
	{ 4, 1 }, // up
	{ 5, 1 }, // dn
} };

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A cube map's faces from an equirectangular panorama, as the VTF stores them.
//-----------------------------------------------------------------------------
std::vector<texture::CFloatImage> CubeMapFromPanorama( const texture::CFloatImage &panorama, int nSize, float flYaw )
{
	std::vector<texture::CFloatImage> stored( 6 );
	if ( panorama.Empty() || nSize <= 0 )
	{
		return {};
	}
	const float flTurn = flYaw * std::numbers::pi_v<float> / 180.0f;
	// Bilinear, wrapping round the sides.
	const auto sample = [ &panorama ]( float u, float v, float *pOut ) {
		const float x = u * static_cast<float>( panorama.m_nWidth ) - 0.5f;
		const float y = std::clamp( v * static_cast<float>( panorama.m_nHeight ) - 0.5f, 0.0f, static_cast<float>( panorama.m_nHeight - 1 ) );
		const int x0 = static_cast<int>( std::floor( x ) );
		const int y0 = static_cast<int>( std::floor( y ) );
		const float fx = x - static_cast<float>( x0 );
		const float fy = y - static_cast<float>( y0 );
		const auto at = [ &panorama ]( int px, int py ) {
			px = ( ( px % panorama.m_nWidth ) + panorama.m_nWidth ) % panorama.m_nWidth;
			py = std::clamp( py, 0, panorama.m_nHeight - 1 );
			return panorama.Pixel( px, py );
		};
		for ( int c = 0; c < 4; c++ )
		{
			const float flTop = at( x0, y0 )[ c ] * ( 1 - fx ) + at( x0 + 1, y0 )[ c ] * fx;
			const float flBottom = at( x0, y0 + 1 )[ c ] * ( 1 - fx ) + at( x0 + 1, y0 + 1 )[ c ] * fx;
			pOut[ c ] = flTop * ( 1 - fy ) + flBottom * fy;
		}
	};
	for ( const Face_t nFace : FACES )
	{
		// The face as the sky sees it, then turned and mirrored back to how the VTF stores it.
		const FaceBasis_t &basis = Basis( nFace );
		texture::CFloatImage face;
		face.m_nWidth = nSize;
		face.m_nHeight = nSize;
		face.m_Pixels.resize( static_cast<std::size_t>( nSize ) * static_cast<std::size_t>( nSize ) * 4 );
		for ( int y = 0; y < nSize; y++ )
		{
			for ( int x = 0; x < nSize; x++ )
			{
				const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( nSize ) * 2.0f - 1.0f;
				const float v = 1.0f - ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( nSize ) * 2.0f;
				const CVec3 vecDirection = basis.m_vecNormal + basis.m_vecRight * u + basis.m_vecUp * v;
				const float flAround = std::atan2( vecDirection.m_flY, vecDirection.m_flX ) + flTurn;
				const float flUp = std::atan2( vecDirection.m_flZ, std::sqrt( vecDirection.m_flX * vecDirection.m_flX + vecDirection.m_flY * vecDirection.m_flY ) );
				float flU = 0.5f - flAround / ( 2.0f * std::numbers::pi_v<float> );
				flU -= std::floor( flU );
				sample( flU, 0.5f - flUp / std::numbers::pi_v<float>, face.m_Pixels.data() + ( static_cast<std::size_t>( y ) * static_cast<std::size_t>( nSize ) + static_cast<std::size_t>( x ) ) * 4 );
			}
		}
		const Stored_t &where = STORED[ IndexOf( nFace ) ];
		stored[ static_cast<std::size_t>( where.m_nIndex ) ] = texture::Oriented( texture::Oriented( face, 0, true ), 4 - where.m_nQuarterTurns, false );
	}
	return stored;
}

//-----------------------------------------------------------------------------
// Purpose: One face of a cube map as a sky face image.
//-----------------------------------------------------------------------------
std::optional<CImage> CubeMapFace( const texture::CVtf &vtf, Face_t nFace, std::string *pError, int nMip, int nFrame )
{
	const Stored_t &stored = STORED[ IndexOf( nFace ) ];
	auto image = vtf.Decode( nMip, nFrame, stored.m_nIndex, pError );
	if ( !image )
	{
		return std::nullopt;
	}
	return texture::Oriented( *image, stored.m_nQuarterTurns, true );
}

} // namespace xv::sky
