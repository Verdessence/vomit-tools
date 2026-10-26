//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Independent SMD/OBJ/glTF/QC serialization using VDC format facts and decoded models.
//===========================================================================//
#include "Export.h"
#include "Binary.h"
#include "material/Preview.h"
#include "kv1/Kv1.h"
#include "vfs/Vfs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
namespace xv::model
{
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Emits stable decimal text regardless of the user's language and numeric locale.
//-----------------------------------------------------------------------------
std::ostringstream Text()
{
	std::ostringstream out;
	out.imbue( std::locale::classic() );
	out << std::fixed << std::setprecision( 6 );
	return out;
}
//-----------------------------------------------------------------------------
// Purpose: Quotes strings for JSON and QC without allowing additional commands or lines.
//-----------------------------------------------------------------------------
std::string Quote( std::string_view szValue )
{
	std::string out = "\"";
	for ( unsigned char ch : szValue )
	{
		switch ( ch )
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ( ch < 32 )
					throw std::runtime_error( "A source name contains a control character" );
				out += static_cast<char>( ch );
		}
	}
	return out + '"';
}
//-----------------------------------------------------------------------------
// Purpose: Packs generated text in the same byte container as compiled files.
//-----------------------------------------------------------------------------
Bytes_t Bytes( std::string_view szText )
{
	return { reinterpret_cast<const std::byte *>( szText.data() ), reinterpret_cast<const std::byte *>( szText.data() + szText.size() ) };
}
//-----------------------------------------------------------------------------
// Purpose: Rejects geometry that a source format cannot represent safely.
//-----------------------------------------------------------------------------
void Check( const CModel &model )
{
	if ( model.m_Vertices.size() > 2000000 || model.m_Bones.size() > 1024 )
		throw std::runtime_error( "Model exceeds the export limit" );
	for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
	{
		int n = model.m_Bones[ i ].m_nParent;
		std::set<int> chain;
		while ( n >= 0 )
		{
			if ( n >= static_cast<int>( model.m_Bones.size() ) || !chain.insert( n ).second || n == static_cast<int>( i ) )
				throw std::runtime_error( "Invalid export bone hierarchy" );
			n = model.m_Bones[ n ].m_nParent;
		}
	}
	for ( const auto &mesh : model.m_Meshes )
	{
		if ( mesh.m_Indices.size() % 3 )
			throw std::runtime_error( "Mesh is not a triangle list" );
		for ( auto n : mesh.m_Indices )
			if ( n >= model.m_Vertices.size() )
				throw std::runtime_error( "Invalid export vertex index" );
	}
	for ( const auto &v : model.m_Vertices )
	{
		for ( float f : { v.m_vecPosition.m_flX, v.m_vecPosition.m_flY, v.m_vecPosition.m_flZ, v.m_vecNormal.m_flX, v.m_vecNormal.m_flY, v.m_vecNormal.m_flZ, v.m_Uv[ 0 ], v.m_Uv[ 1 ] } )
			if ( !std::isfinite( f ) )
				throw std::runtime_error( "Non-finite export vertex" );
		if ( v.m_nBones < 0 || v.m_nBones > 3 )
			throw std::runtime_error( "A vertex has more than three bone weights" );
		for ( int i = 0; i < v.m_nBones; i++ )
			if ( v.m_Bones[ i ] < 0 || v.m_Bones[ i ] >= static_cast<int>( model.m_Bones.size() ) || !std::isfinite( v.m_Weights[ i ] ) || v.m_Weights[ i ] < 0 )
				throw std::runtime_error( "Invalid export vertex weight" );
	}
}
//-----------------------------------------------------------------------------
// Purpose: Keeps model-local skeleton transforms consistent across all exported formats.
//-----------------------------------------------------------------------------
CTransform Local( const CModel &model, std::size_t i, std::span<const CTransform> pose = {} )
{
	const auto transform = pose.empty() ? model.m_Bones[ i ].m_ToModel : pose[ i ];
	const int nParent = model.m_Bones[ i ].m_nParent;
	if ( nParent < 0 )
		return transform;
	const auto parent = pose.empty() ? model.m_Bones[ nParent ].m_ToModel : pose[ nParent ];
	const auto inverse = parent.Inverse();
	if ( !inverse )
		throw std::runtime_error( "Singular bone transform" );
	return *inverse * transform;
}
//-----------------------------------------------------------------------------
// Purpose: Writes all nodes, including unweighted helper bones.
//-----------------------------------------------------------------------------
void Nodes( std::ostringstream &out, const CModel &model )
{
	out << "version 1\r\nnodes\r\n";
	if ( model.m_Bones.empty() )
		out << "0 \"root\" -1\r\n";
	for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
		out << i << ' ' << Quote( model.m_Bones[ i ].m_szName ) << ' ' << model.m_Bones[ i ].m_nParent << "\r\n";
	out << "end\r\nskeleton\r\n";
}
//-----------------------------------------------------------------------------
// Purpose: Serializes a local pose as SMD positions and XYZ Euler radians.
//-----------------------------------------------------------------------------
void Pose( std::ostringstream &out, const CModel &model, int nFrame, std::span<const CTransform> pose = {} )
{
	out << "time " << nFrame << "\r\n";
	if ( model.m_Bones.empty() )
		out << "0 0 0 0 0 0 0\r\n";
	for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
	{
		const auto local = Local( model, i, pose );
		const auto p = local.GetOrigin(), a = EulerAngles( local );
		out << i << ' ' << p.m_flX << ' ' << p.m_flY << ' ' << p.m_flZ << ' ' << a.m_flX << ' ' << a.m_flY << ' ' << a.m_flZ << "\r\n";
	}
}
//-----------------------------------------------------------------------------
// Purpose: Determines clip duration and validates explicit frame trims before generating files.
//-----------------------------------------------------------------------------
std::pair<int, int> ClipRange( const CStudioFile &source, const Clip_t &clip )
{
	if ( clip.m_nSequence < 0 || clip.m_nSequence >= static_cast<int>( source.m_Sequences.size() ) )
		throw std::runtime_error( "Select an animation sequence" );
	const auto &seq = source.m_Sequences[ clip.m_nSequence ];
	if ( seq.m_bHasLayers && !clip.m_bAllowBaseBlend )
		throw std::runtime_error( "This sequence has layers; explicitly select base-blend export or use the original QC source" );
	if ( seq.m_Animations.empty() )
		throw std::runtime_error( "This sequence has no local animation" );
	int nFrames = 1;
	for ( int n : seq.m_Animations )
	{
		if ( n < 0 || n >= static_cast<int>( source.m_Animations.size() ) )
			throw std::runtime_error( "Invalid animation reference" );
		nFrames = std::max( nFrames, source.m_Animations[ n ].m_nFrames );
	}
	const int nLast = clip.m_nLast < 0 ? nFrames - 1 : clip.m_nLast;
	if ( clip.m_nFirst < 0 || nLast < clip.m_nFirst || nLast >= nFrames || nLast - clip.m_nFirst > 100000 )
		throw std::runtime_error( "Animation frame range is invalid" );
	if ( static_cast<std::size_t>( nLast - clip.m_nFirst + 1 ) * source.m_Bones.size() > 2000000 )
		throw std::runtime_error( "Clip exceeds the export limit; select a shorter frame range" );
	return { nFrames, nLast };
}
//-----------------------------------------------------------------------------
// Purpose: Decodes a local rotation from its stored bind quaternion.
//-----------------------------------------------------------------------------
CTransform BindLocal( const StudioBone_t &bone )
{
	const auto &[ x, y, z, w ] = bone.m_Quaternion;
	CTransform t;
	t.m_Values = { 1 - 2 * ( y * y + z * z ), 2 * ( x * y - z * w ), 2 * ( x * z + y * w ), bone.m_vecPosition.m_flX, 2 * ( x * y + z * w ), 1 - 2 * ( x * x + z * z ), 2 * ( y * z - x * w ), bone.m_vecPosition.m_flY, 2 * ( x * z - y * w ), 2 * ( y * z + x * w ), 1 - 2 * ( x * x + y * y ), bone.m_vecPosition.m_flZ };
	return t;
}
//-----------------------------------------------------------------------------
// Purpose: Applies source motion deltas to target bind transforms, matched by bone name.
//-----------------------------------------------------------------------------
std::vector<CTransform> Retarget( const CStudioFile &source, const CModel &target, float flCycle, const Clip_t &clip )
{
	std::string szError;
	const auto sampled = SampleSequence( source, clip.m_nSequence, flCycle, clip.m_Parameters, target, &szError );
	if ( !sampled )
		throw std::runtime_error( szError );
	std::vector<CTransform> locals( target.m_Bones.size() ), world( target.m_Bones.size() );
	int nMatched = 0;
	for ( std::size_t i = 0; i < target.m_Bones.size(); i++ )
	{
		const auto found = std::ranges::find( source.m_Bones, target.m_Bones[ i ].m_szName, &StudioBone_t::m_szName );
		locals[ i ] = Local( target, i );
		if ( found != source.m_Bones.end() )
		{
			const auto inverse = BindLocal( *found ).Inverse();
			if ( !inverse )
				throw std::runtime_error( "Singular source bind pose" );
			locals[ i ] = locals[ i ] * *inverse * Local( target, i, *sampled );
			nMatched++;
		}
	}
	if ( !target.m_Bones.empty() && !nMatched )
		throw std::runtime_error( "No source bones match the target skeleton" );
	std::vector<bool> done( world.size() );
	const auto build = [ & ]( auto &&self, std::size_t i ) -> void {if(done[i])return;const int n=target.m_Bones[i].m_nParent;if(n>=0)self(self,n);world[i]=n>=0?world[n]*locals[i]:locals[i];done[i]=true; };
	for ( std::size_t i = 0; i < world.size(); i++ )
		build( build, i );
	return world;
}
//-----------------------------------------------------------------------------
// Purpose: Encodes a binary glTF buffer without auxiliary binary files.
//-----------------------------------------------------------------------------
std::string Base64( std::span<const std::byte> bytes )
{
	constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for ( std::size_t i = 0; i < bytes.size(); i += 3 )
	{
		const auto a = std::to_integer<unsigned>( bytes[ i ] ), b = i + 1 < bytes.size() ? std::to_integer<unsigned>( bytes[ i + 1 ] ) : 0, c = i + 2 < bytes.size() ? std::to_integer<unsigned>( bytes[ i + 2 ] ) : 0;
		const auto n = ( a << 16 ) | ( b << 8 ) | c;
		out += alphabet[ ( n >> 18 ) & 63 ];
		out += alphabet[ ( n >> 12 ) & 63 ];
		out += i + 1 < bytes.size() ? alphabet[ ( n >> 6 ) & 63 ] : '=';
		out += i + 2 < bytes.size() ? alphabet[ n & 63 ] : '=';
	}
	return out;
}
//-----------------------------------------------------------------------------
// Purpose: Converts orthonormal bone matrices to unit XYZW quaternions for glTF tracks.
//-----------------------------------------------------------------------------
std::array<float, 4> Quaternion( const CTransform &t )
{
	const auto &m = t.m_Values;
	std::array<float, 4> q{};
	const float flTrace = m[ 0 ] + m[ 5 ] + m[ 10 ];
	if ( flTrace > 0 )
	{
		const float s = 2 * std::sqrt( flTrace + 1 );
		q = { ( m[ 9 ] - m[ 6 ] ) / s, ( m[ 2 ] - m[ 8 ] ) / s, ( m[ 4 ] - m[ 1 ] ) / s, s * 0.25f };
	}
	else
	{
		const int a = m[ 0 ] > m[ 5 ] ? ( m[ 0 ] > m[ 10 ] ? 0 : 2 ) : ( m[ 5 ] > m[ 10 ] ? 1 : 2 ), b = ( a + 1 ) % 3, c = ( a + 2 ) % 3;
		const float s = 2 * std::sqrt( std::max( 0.0f, 1 + m[ a * 4 + a ] - m[ b * 4 + b ] - m[ c * 4 + c ] ) );
		if ( s < 1e-8f )
			return { 0, 0, 0, 1 };
		q[ a ] = s * 0.25f;
		q[ b ] = ( m[ a * 4 + b ] + m[ b * 4 + a ] ) / s;
		q[ c ] = ( m[ a * 4 + c ] + m[ c * 4 + a ] ) / s;
		q[ 3 ] = ( m[ c * 4 + b ] - m[ b * 4 + c ] ) / s;
	}
	return q;
}
//-----------------------------------------------------------------------------
// Purpose: Writes an array of numbers as a JSON array.
//-----------------------------------------------------------------------------
template <class T>
std::string Array( const T &values )
{
	auto out = Text();
	out << '[';
	bool bFirst = true;
	for ( auto v : values )
	{
		if ( !bFirst )
			out << ',';
		out << v;
		bFirst = false;
	}
	out << ']';
	return out.str();
}
//-----------------------------------------------------------------------------
// Purpose: Extracts material base images for exchange formats without changing game textures.
//-----------------------------------------------------------------------------
std::vector<std::string> Textures( Export_t &result, const CModel &model, const Reader_t &read )
{
	std::vector<std::string> paths( model.m_Materials.size() );
	if ( !read )
		return paths;
	for ( std::size_t i = 0; i < paths.size(); i++ )
	{
		const auto bytes = read( "materials/" + model.m_Materials[ i ] );
		if ( !bytes )
		{
			result.m_Notes.push_back( "Missing material: " + model.m_Materials[ i ] );
			continue;
		}
		auto material = material::ParseMaterial( std::string_view( reinterpret_cast<const char *>( bytes->data() ), bytes->size() ), read );
		if ( !material )
			continue;
		const auto textures = material::CTextureSet::Load( *material, read, 4096, 1 );
		const auto *p = textures.Find( "$basetexture" );
		if ( p && !p->m_Frames.empty() )
		{
			paths[ i ] = "texture_" + std::to_string( i ) + ".png";
			result.m_Files[ paths[ i ] ] = texture::SaveImage( p->m_Frames[ 0 ], texture::ImageFileType_t::IMAGE_FILE_TYPE_PNG );
		}
		else
			result.m_Notes.push_back( "No decoded base texture: " + model.m_Materials[ i ] );
	}
	return paths;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Checks a portable relative output path before any files are created.
//-----------------------------------------------------------------------------
bool IsExportPath( std::string_view szPath )
{
	if ( szPath.empty() || szPath.front() == '/' || szPath.front() == '\\' || szPath.find_first_of( ":\r\n\0", 0, 4 ) != std::string_view::npos )
		return false;
	std::string path( szPath );
	std::ranges::replace( path, '\\', '/' );
	std::istringstream parts( path );
	std::string part;
	while ( std::getline( parts, part, '/' ) )
		if ( part == ".." || part.empty() )
			return false;
	return true;
}
//-----------------------------------------------------------------------------
// Purpose: Converts an orthonormal XYZ rotation to SMD radians, including the gimbal-lock case.
//-----------------------------------------------------------------------------
CVec3 EulerAngles( const CTransform &t )
{
	const auto &m = t.m_Values;
	const float y = std::asin( std::clamp( -m[ 8 ], -1.0f, 1.0f ) );
	if ( std::abs( std::cos( y ) ) > 1e-6f )
		return { std::atan2( m[ 9 ], m[ 10 ] ), y, std::atan2( m[ 4 ], m[ 0 ] ) };
	return { std::atan2( -m[ 6 ], m[ 5 ] ), y, 0 };
}
//-----------------------------------------------------------------------------
// Purpose: Exports weighted reference geometry with SMD's clockwise winding and bottom-up UVs.
//-----------------------------------------------------------------------------
std::string ExportSmd( const CModel &model )
{
	Check( model );
	auto out = Text();
	Nodes( out, model );
	Pose( out, model, 0 );
	out << "end\r\ntriangles\r\n";
	for ( const auto &mesh : model.m_Meshes )
		for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
		{
			out << "material_" << std::max( 0, mesh.m_nMaterial ) << "\r\n";
			for ( int j = 0; j < 3; j++ )
			{
				const auto &v = model.m_Vertices[ mesh.m_Indices[ i + j ] ];
				out << ( v.m_nBones ? v.m_Bones[ 0 ] : 0 ) << ' ' << v.m_vecPosition.m_flX << ' ' << v.m_vecPosition.m_flY << ' ' << v.m_vecPosition.m_flZ << ' ' << v.m_vecNormal.m_flX << ' ' << v.m_vecNormal.m_flY << ' ' << v.m_vecNormal.m_flZ << ' ' << v.m_Uv[ 0 ] << ' ' << 1 - v.m_Uv[ 1 ] << ' ' << ( v.m_nBones ? v.m_nBones : 1 );
				if ( !v.m_nBones )
					out << " 0 1";
				for ( int k = 0; k < v.m_nBones; k++ )
					out << ' ' << v.m_Bones[ k ] << ' ' << v.m_Weights[ k ];
				out << "\r\n";
			}
		}
	out << "end\r\n\r\n";
	return out.str();
}
//-----------------------------------------------------------------------------
// Purpose: Writes a trimmed or retargeted sequence as complete local poses, one animation per SMD.
//-----------------------------------------------------------------------------
std::string ExportAnimationSmd( const CStudioFile &source, const CModel &target, const Clip_t &clip )
{
	Check( target );
	const auto [ nFrames, nLast ] = ClipRange( source, clip );
	auto out = Text();
	Nodes( out, target );
	for ( int i = clip.m_nFirst; i <= nLast; i++ )
	{
		const auto pose = Retarget( source, target, nFrames > 1 ? static_cast<float>( i ) / ( nFrames - 1 ) : 0, clip );
		Pose( out, target, i - clip.m_nFirst, pose );
	}
	out << "end\r\n\r\n";
	return out.str();
}
//-----------------------------------------------------------------------------
// Purpose: Writes OBJ, MTL and optional base textures for the current body, skin and detail level.
//-----------------------------------------------------------------------------
Export_t ExportObj( const CModel &model, const Reader_t &read )
{
	Check( model );
	Export_t result;
	const auto textures = Textures( result, model, read );
	auto obj = Text(), mtl = Text();
	obj << "mtllib model.mtl\n";
	for ( const auto &v : model.m_Vertices )
		obj << "v " << v.m_vecPosition.m_flX << ' ' << v.m_vecPosition.m_flY << ' ' << v.m_vecPosition.m_flZ << '\n';
	for ( const auto &v : model.m_Vertices )
		obj << "vt " << v.m_Uv[ 0 ] << ' ' << 1 - v.m_Uv[ 1 ] << '\n';
	for ( const auto &v : model.m_Vertices )
		obj << "vn " << v.m_vecNormal.m_flX << ' ' << v.m_vecNormal.m_flY << ' ' << v.m_vecNormal.m_flZ << '\n';
	for ( std::size_t i = 0; i < std::max<std::size_t>( 1, model.m_Materials.size() ); i++ )
	{
		mtl << "newmtl material_" << i << "\nKd 1 1 1\n";
		if ( i < textures.size() && !textures[ i ].empty() )
			mtl << "map_Kd " << textures[ i ] << '\n';
	}
	for ( const auto &mesh : model.m_Meshes )
	{
		obj << "usemtl material_" << std::max( 0, mesh.m_nMaterial ) << '\n';
		for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
		{
			obj << 'f';
			for ( int j : { 0, 2, 1 } )
			{
				const auto n = mesh.m_Indices[ i + j ] + 1;
				obj << ' ' << n << '/' << n << '/' << n;
			}
			obj << '\n';
		}
	}
	result.m_Files[ "model.obj" ] = Bytes( obj.str() );
	result.m_Files[ "model.mtl" ] = Bytes( mtl.str() );
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Writes glTF 2.0 with a bind skeleton, three vertex weights, optional tracks and images.
//-----------------------------------------------------------------------------
Export_t ExportGltf( const CModel &model, const Reader_t &read, const CStudioFile *pSource, const Clip_t &clip )
{
	Check( model );
	if ( model.m_Materials.empty() )
	{
		auto fallback = model;
		fallback.m_Materials.emplace_back( "material_0" );
		for ( auto &mesh : fallback.m_Meshes )
			mesh.m_nMaterial = 0;
		return ExportGltf( fallback, read, pSource, clip );
	}
	Export_t result;
	const auto textures = Textures( result, model, read );
	Bytes_t buffer;
	std::vector<std::string> views, accessors;
	const auto accessor = [ & ]<class T>( const std::vector<T> &data, int nComponent, int nCount, const char *pszType, const std::string &szExtra = "" ) {
		while ( buffer.size() % 4 )
			buffer.push_back( std::byte{} );
		const auto nAt = buffer.size();
		const auto *p = reinterpret_cast<const std::byte *>( data.data() );
		buffer.insert( buffer.end(), p, p + data.size() * sizeof( T ) );
		views.push_back( "{\"buffer\":0,\"byteOffset\":" + std::to_string( nAt ) + ",\"byteLength\":" + std::to_string( data.size() * sizeof( T ) ) + "}" );
		accessors.push_back( "{\"bufferView\":" + std::to_string( views.size() - 1 ) + ",\"componentType\":" + std::to_string( nComponent ) + ",\"count\":" + std::to_string( nCount ) + ",\"type\":" + Quote( pszType ) + szExtra + "}" );
		return static_cast<int>( accessors.size() - 1 );
	};
	std::vector<float> positions, normals, uvs, weights;
	std::vector<std::uint16_t> joints;
	CVec3 vecMin{}, vecMax{};
	bool bFirst = true;
	for ( const auto &v : model.m_Vertices )
	{
		positions.insert( positions.end(), { v.m_vecPosition.m_flX, v.m_vecPosition.m_flY, v.m_vecPosition.m_flZ } );
		normals.insert( normals.end(), { v.m_vecNormal.m_flX, v.m_vecNormal.m_flY, v.m_vecNormal.m_flZ } );
		uvs.insert( uvs.end(), { v.m_Uv[ 0 ], v.m_Uv[ 1 ] } );
		float flTotal = 0;
		for ( int j = 0; j < v.m_nBones; j++ )
			flTotal += v.m_Weights[ j ];
		for ( int j = 0; j < 4; j++ )
		{
			joints.push_back( j < v.m_nBones ? v.m_Bones[ j ] : 0 );
			weights.push_back( flTotal > 0 && j < v.m_nBones ? v.m_Weights[ j ] / flTotal : flTotal <= 0 && j == 0 ? 1.0f
																												   : 0.0f );
		}
		if ( bFirst )
			vecMin = vecMax = v.m_vecPosition;
		vecMin = { std::min( vecMin.m_flX, v.m_vecPosition.m_flX ), std::min( vecMin.m_flY, v.m_vecPosition.m_flY ), std::min( vecMin.m_flZ, v.m_vecPosition.m_flZ ) };
		vecMax = { std::max( vecMax.m_flX, v.m_vecPosition.m_flX ), std::max( vecMax.m_flY, v.m_vecPosition.m_flY ), std::max( vecMax.m_flZ, v.m_vecPosition.m_flZ ) };
		bFirst = false;
	}
	if ( positions.empty() )
		throw std::runtime_error( "There are no vertices to export" );
	const int nPositions = accessor( positions, 5126, model.m_Vertices.size(), "VEC3", ",\"min\":" + Array( std::array{ vecMin.m_flX, vecMin.m_flY, vecMin.m_flZ } ) + ",\"max\":" + Array( std::array{ vecMax.m_flX, vecMax.m_flY, vecMax.m_flZ } ) ), nNormals = accessor( normals, 5126, model.m_Vertices.size(), "VEC3" ), nUv = accessor( uvs, 5126, model.m_Vertices.size(), "VEC2" );
	const bool bSkin = !model.m_Bones.empty();
	const int nJoints = bSkin ? accessor( joints, 5123, model.m_Vertices.size(), "VEC4" ) : -1, nWeights = bSkin ? accessor( weights, 5126, model.m_Vertices.size(), "VEC4" ) : -1;
	auto out = Text();
	out << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"vomit-tools\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"Source units to glTF metres\",\"matrix\":[0.0254,0,0,0,0,0,-0.0254,0,0,0.0254,0,0,0,0,0,1],\"children\":[1";
	for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
		if ( model.m_Bones[ i ].m_nParent < 0 )
			out << ',' << i + 2;
	out << "]},{\"mesh\":0";
	if ( bSkin )
		out << ",\"skin\":0";
	out << '}';
	for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
	{
		const auto local = Local( model, i );
		const auto p = local.GetOrigin();
		out << ",{\"name\":" << Quote( model.m_Bones[ i ].m_szName ) << ",\"translation\":" << Array( std::array{ p.m_flX, p.m_flY, p.m_flZ } ) << ",\"rotation\":" << Array( Quaternion( local ) );
		std::vector<int> children;
		for ( std::size_t j = 0; j < model.m_Bones.size(); j++ )
			if ( model.m_Bones[ j ].m_nParent == static_cast<int>( i ) )
				children.push_back( j + 2 );
		if ( !children.empty() )
			out << ",\"children\":" << Array( children );
		out << '}';
	}
	out << "],\"meshes\":[{\"primitives\":[";
	bool bMeshFirst = true;
	for ( const auto &mesh : model.m_Meshes )
	{
		std::vector<std::uint32_t> indices;
		for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
			indices.insert( indices.end(), { mesh.m_Indices[ i ], mesh.m_Indices[ i + 2 ], mesh.m_Indices[ i + 1 ] } );
		if ( indices.empty() )
			continue;
		const int nIndices = accessor( indices, 5125, indices.size(), "SCALAR" );
		if ( !bMeshFirst )
			out << ',';
		bMeshFirst = false;
		out << "{\"attributes\":{\"POSITION\":" << nPositions << ",\"NORMAL\":" << nNormals << ",\"TEXCOORD_0\":" << nUv;
		if ( bSkin )
			out << ",\"JOINTS_0\":" << nJoints << ",\"WEIGHTS_0\":" << nWeights;
		out << "},\"indices\":" << nIndices;
		if ( mesh.m_nMaterial >= 0 && mesh.m_nMaterial < static_cast<int>( model.m_Materials.size() ) )
			out << ",\"material\":" << mesh.m_nMaterial;
		out << '}';
	}
	out << "]}]";
	if ( bSkin )
	{
		std::vector<float> inverse;
		std::vector<int> nodes;
		for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
		{
			const auto t = model.m_Bones[ i ].m_ToModel.Inverse();
			if ( !t )
				throw std::runtime_error( "Singular inverse bind matrix" );
			for ( int c = 0; c < 4; c++ )
				for ( int r = 0; r < 4; r++ )
					inverse.push_back( r < 3 ? t->m_Values[ r * 4 + c ] : c == 3 ? 1.0f
																				 : 0.0f );
			nodes.push_back( i + 2 );
		}
		const int nBind = accessor( inverse, 5126, model.m_Bones.size(), "MAT4" );
		out << ",\"skins\":[{\"inverseBindMatrices\":" << nBind << ",\"joints\":" << Array( nodes ) << "}]";
	}
	if ( pSource && clip.m_nSequence >= 0 && bSkin )
	{
		const auto [ nFrames, nLast ] = ClipRange( *pSource, clip );
		float flFps = clip.m_flFps;
		if ( flFps <= 0 )
			flFps = pSource->m_Animations[ pSource->m_Sequences[ clip.m_nSequence ].m_Animations[ 0 ] ].m_flFps;
		if ( !std::isfinite( flFps ) || flFps <= 0 )
			throw std::runtime_error( "Invalid animation fps" );
		std::vector<float> times;
		std::vector<std::vector<float>> translations( model.m_Bones.size() ), rotations( model.m_Bones.size() );
		for ( int f = clip.m_nFirst; f <= nLast; f++ )
		{
			times.push_back( ( f - clip.m_nFirst ) / flFps );
			const auto pose = Retarget( *pSource, model, nFrames > 1 ? static_cast<float>( f ) / ( nFrames - 1 ) : 0, clip );
			for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
			{
				const auto local = Local( model, i, pose );
				const auto p = local.GetOrigin();
				translations[ i ].insert( translations[ i ].end(), { p.m_flX, p.m_flY, p.m_flZ } );
				const auto q = Quaternion( local );
				rotations[ i ].insert( rotations[ i ].end(), q.begin(), q.end() );
			}
		}
		const int nTimes = accessor( times, 5126, times.size(), "SCALAR", ",\"min\":[0],\"max\":" + Array( std::array{ times.back() } ) );
		out << ",\"animations\":[{\"samplers\":[";
		for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
		{
			const int p = accessor( translations[ i ], 5126, times.size(), "VEC3" ), q = accessor( rotations[ i ], 5126, times.size(), "VEC4" );
			if ( i )
				out << ',';
			out << "{\"input\":" << nTimes << ",\"output\":" << p << "},{\"input\":" << nTimes << ",\"output\":" << q << '}';
		}
		out << "],\"channels\":[";
		for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
		{
			if ( i )
				out << ',';
			out << "{\"sampler\":" << i * 2 << ",\"target\":{\"node\":" << i + 2 << ",\"path\":\"translation\"}},{\"sampler\":" << i * 2 + 1 << ",\"target\":{\"node\":" << i + 2 << ",\"path\":\"rotation\"}}";
		}
		out << "]}]";
	}
	out << ",\"materials\":[";
	for ( std::size_t i = 0; i < model.m_Materials.size(); i++ )
	{
		if ( i )
			out << ',';
		out << "{\"name\":" << Quote( model.m_Materials[ i ] ) << ",\"pbrMetallicRoughness\":{\"metallicFactor\":0,\"roughnessFactor\":1";
		if ( !textures[ i ].empty() )
			out << ",\"baseColorTexture\":{\"index\":" << i << '}';
		out << "}}";
	}
	out << "],\"textures\":[";
	for ( std::size_t i = 0; i < textures.size(); i++ )
	{
		if ( i )
			out << ',';
		out << "{\"source\":" << i << '}';
	}
	out << "],\"images\":[";
	for ( std::size_t i = 0; i < textures.size(); i++ )
	{
		if ( i )
			out << ',';
		out << "{\"uri\":" << Quote( textures[ i ].empty() ? "missing.png" : textures[ i ] ) << '}';
	}
	out << "],\"bufferViews\":[";
	for ( std::size_t i = 0; i < views.size(); i++ )
	{
		if ( i )
			out << ',';
		out << views[ i ];
	}
	out << "],\"accessors\":[";
	for ( std::size_t i = 0; i < accessors.size(); i++ )
	{
		if ( i )
			out << ',';
		out << accessors[ i ];
	}
	out << "],\"buffers\":[{\"byteLength\":" << buffer.size() << ",\"uri\":\"data:application/octet-stream;base64," << Base64( buffer ) << "\"}]}";
	result.m_Files[ "model.gltf" ] = Bytes( out.str() );
	if ( std::ranges::find( textures, std::string{} ) != textures.end() )
	{
		texture::CImage white( 1, 1 );
		std::ranges::fill( white.m_Pixels, std::uint8_t{ 255 } );
		result.m_Files[ "missing.png" ] = texture::SaveImage( white, texture::ImageFileType_t::IMAGE_FILE_TYPE_PNG );
	}
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Exports every bodygroup choice as editable sources, with optional stage-three inputs.
//-----------------------------------------------------------------------------
Export_t ExportSourceProject( std::string_view szPath, const Reader_t &read, const SourceOptions_t &options )
{
	std::string szError;
	const auto file = ReadStudio( szPath, read, &szError );
	if ( !file )
		throw std::runtime_error( szError );
	if ( !IsExportPath( options.m_szModelName ) || !options.m_szModelName.ends_with( ".mdl" ) )
		throw std::runtime_error( "Choose a relative model name ending in .mdl" );
	auto base = LoadModel( szPath, read, &szError );
	if ( !base )
		throw std::runtime_error( szError );
	Export_t result;
	std::vector<std::string> materials = base->m_Materials;
	auto qc = Text();
	qc << "// Editable source reconstruction. See export-notes.txt before recompiling.\n$modelname " << Quote( options.m_szModelName ) << "\n$cdmaterials \"\"\n$surfaceprop " << Quote( file->m_szSurface ) << '\n';
	if ( file->m_nFlags & 16 )
		qc << "$staticprop\n";
	const auto meshText = [ & ]( CModel mesh ) {Check(mesh);std::vector<int> map;for(const auto &szMaterial:mesh.m_Materials){auto found=std::ranges::find(materials,szMaterial);if(found==materials.end()){map.push_back(materials.size());materials.push_back(szMaterial);}else map.push_back(found-materials.begin());}for(auto &part:mesh.m_Meshes)part.m_nMaterial=part.m_nMaterial>=0&&part.m_nMaterial<static_cast<int>(map.size())?map[part.m_nMaterial]:0;return ExportSmd(mesh); };
	for ( std::size_t p = 0; p < file->m_Bodygroups.size(); p++ )
	{
		const auto &group = file->m_Bodygroups[ p ];
		qc << "$bodygroup " << Quote( group.m_szName ) << "\n{\n";
		for ( std::size_t m = 0; m < group.m_Models.size(); m++ )
		{
			if ( !options.m_szMeshSource.empty() && !options.m_bAppendBodygroup && options.m_nBodygroup == static_cast<int>( p ) && options.m_nBodyModel == static_cast<int>( m ) )
			{
				qc << " studio " << Quote( options.m_szMeshSource ) << '\n';
				continue;
			}
			Selection_t selection;
			selection.m_Bodygroups.resize( file->m_Bodygroups.size() );
			selection.m_Bodygroups[ p ] = m;
			auto loaded = LoadModel( szPath, read, &szError, selection );
			if ( !loaded )
				throw std::runtime_error( szError );
			std::erase_if( loaded->m_Meshes, [ p, m ]( const auto &mesh ) { return mesh.m_nBodygroup != static_cast<int>( p ) || mesh.m_nBodyModel != static_cast<int>( m ); } );
			if ( options.m_pReplacement && !options.m_bAppendBodygroup && options.m_nBodygroup == static_cast<int>( p ) && options.m_nBodyModel == static_cast<int>( m ) )
				*loaded = *options.m_pReplacement;
			if ( loaded->m_Meshes.empty() )
			{
				qc << " blank\n";
				continue;
			}
			const auto name = "body_" + std::to_string( p ) + "_" + std::to_string( m ) + ".smd";
			result.m_Files[ name ] = Bytes( meshText( std::move( *loaded ) ) );
			qc << " studio " << Quote( name ) << '\n';
		}
		qc << "}\n";
	}
	if ( !options.m_szMeshSource.empty() && options.m_bAppendBodygroup )
		qc << "$bodygroup imported\n{\n blank\n studio " << Quote( options.m_szMeshSource ) << "\n}\n";
	if ( options.m_pReplacement && options.m_bAppendBodygroup )
	{
		result.m_Files[ "imported.smd" ] = Bytes( meshText( *options.m_pReplacement ) );
		qc << "$bodygroup imported\n{\n blank\n studio \"imported.smd\"\n}\n";
	}
	result.m_Files[ "reference.smd" ] = Bytes( meshText( *base ) );
	if ( options.m_pAnimationSource && options.m_Clip.m_nSequence >= 0 )
	{
		result.m_Files[ "animation.smd" ] = Bytes( ExportAnimationSmd( *options.m_pAnimationSource, *base, options.m_Clip ) );
		const auto &seq = options.m_pAnimationSource->m_Sequences.at( options.m_Clip.m_nSequence );
		float flFps = options.m_Clip.m_flFps;
		if ( flFps <= 0 )
			flFps = options.m_pAnimationSource->m_Animations.at( seq.m_Animations.at( 0 ) ).m_flFps;
		qc << "$sequence " << Quote( seq.m_szName ) << " \"animation.smd\" fps " << flFps << ( seq.m_nFlags & 1 ? " loop" : "" ) << '\n';
	}
	else
		qc << "$sequence idle \"reference.smd\" fps 30\n";
	// Collision materials must join the alias table before it is serialized.
	if ( options.m_pCollision )
		result.m_Files[ "collision.smd" ] = Bytes( meshText( *options.m_pCollision ) );
	for ( std::size_t i = 0; i < materials.size(); i++ )
	{
		auto name = materials[ i ];
		if ( name.ends_with( ".vmt" ) )
			name.resize( name.size() - 4 );
		qc << "$renamematerial material_" << i << ' ' << Quote( name ) << '\n';
	}
	if ( file->m_Skins.size() > 1 )
	{
		qc << "$texturegroup skinfamilies\n{\n";
		for ( const auto &skin : file->m_Skins )
		{
			qc << " {";
			for ( int nMaterial : skin )
			{
				if ( nMaterial < 0 || nMaterial >= static_cast<int>( base->m_Materials.size() ) )
					throw std::runtime_error( "Skin references an invalid material" );
				auto name = base->m_Materials[ nMaterial ];
				if ( name.ends_with( ".vmt" ) )
					name.resize( name.size() - 4 );
				qc << ' ' << Quote( name );
			}
			qc << " }\n";
		}
		qc << "}\n";
	}
	for ( const auto &set : file->m_HitboxSets )
	{
		qc << "$hboxset " << Quote( set.m_szName ) << '\n';
		for ( const auto &box : set.m_Boxes )
			if ( box.m_nBone >= 0 && box.m_nBone < static_cast<int>( file->m_Bones.size() ) )
				qc << "$hbox " << box.m_nGroup << ' ' << Quote( file->m_Bones[ box.m_nBone ].m_szName ) << ' ' << box.m_vecMin.m_flX << ' ' << box.m_vecMin.m_flY << ' ' << box.m_vecMin.m_flZ << ' ' << box.m_vecMax.m_flX << ' ' << box.m_vecMax.m_flY << ' ' << box.m_vecMax.m_flZ << '\n';
	}
	for ( const auto &attachment : base->m_Attachments )
		if ( attachment.m_nBone >= 0 && attachment.m_nBone < static_cast<int>( base->m_Bones.size() ) )
		{
			const auto p = attachment.m_Local.GetOrigin(), a = EulerAngles( attachment.m_Local ) * ( 180.0f / 3.14159265358979323846f );
			qc << "$attachment " << Quote( attachment.m_szName ) << ' ' << Quote( base->m_Bones[ attachment.m_nBone ].m_szName ) << ' ' << p.m_flX << ' ' << p.m_flY << ' ' << p.m_flZ << " rotate " << a.m_flY << ' ' << a.m_flZ << ' ' << a.m_flX << '\n';
		}
	if ( options.m_pCollision )
	{
		qc << "$collisionmodel \"collision.smd\"\n{\n $mass " << file->m_flMass << "\n $concave\n}\n";
	}
	if ( !options.m_szCollisionSource.empty() )
		qc << "$collisionmodel " << Quote( options.m_szCollisionSource ) << "\n{\n $mass " << file->m_flMass << "\n $concave\n}\n";
	for ( const auto &folder : file->m_Folders )
		qc << "$cdmaterials " << Quote( folder ) << '\n';
	result.m_Files[ "model.qc" ] = Bytes( qc.str() );
	result.m_Notes = { "This is an editable source reconstruction, not a byte-identical decompile.", "Only the requested animation clip is emitted. Edit model.qc to add other animations, LODs, IK, procedural bones, flexes, eyes or ragdoll constraints.", "Original physics is omitted unless a collision source is supplied. Studiomdl generates the replacement hull; no collision compiler is implemented here.", "External SMD, OBJ and DMX sources remain references to their original paths; keep them with the project.", "The compiled input files have not been modified." };
	std::string notes;
	for ( const auto &note : result.m_Notes )
		notes += note + '\n';
	result.m_Files[ "export-notes.txt" ] = Bytes( notes );
	return result;
}
//-----------------------------------------------------------------------------
// Purpose: Collects complete model/material dependency closures while bounding cycles and paths.
//-----------------------------------------------------------------------------
Export_t CollectModelFiles( std::string_view szPath, const Reader_t &read )
{
	Export_t result;
	std::vector<std::string> pending{ vfs::NormalizePath( szPath ) };
	std::set<std::string> seen;
	std::size_t nTotal = 0;
	for ( std::size_t i = 0; i < pending.size(); i++ )
	{
		const auto path = vfs::NormalizePath( pending[ i ] );
		if ( !IsExportPath( path ) )
			throw std::runtime_error( "Unsafe dependency path: " + path );
		if ( !seen.insert( path ).second )
			continue;
		if ( seen.size() > 65536 )
			throw std::runtime_error( "Dependency graph exceeds the file limit" );
		const auto bytes = read( path );
		if ( !bytes )
		{
			result.m_Notes.push_back( "Missing dependency: " + path );
			continue;
		}
		nTotal += bytes->size();
		if ( nTotal > static_cast<std::size_t>( 2 ) * 1024 * 1024 * 1024 )
			throw std::runtime_error( "Dependencies exceed 2 GiB" );
		result.m_Files[ path ] = *bytes;
		if ( path.ends_with( ".mdl" ) )
		{
			std::string error;
			const auto studio = ReadStudio( path, read, &error );
			if ( !studio )
				throw std::runtime_error( error );
			const auto base = path.substr( 0, path.size() - 4 );
			for ( const auto *ext : { ".vvd", ".dx90.vtx", ".dx80.vtx", ".dx7_2bone.vtx", ".dx7_3bone.vtx", ".sw.vtx", ".phy" } )
				if ( read( base + ext ) )
					pending.push_back( base + ext );
			if ( !studio->m_szAnimationFile.empty() )
				pending.push_back( studio->m_szAnimationFile );
			for ( auto include : studio->m_Includes )
			{
				if ( !include.starts_with( "models/" ) )
					include = "models/" + include;
				pending.push_back( include );
			}
			if ( const auto model = LoadModel( path, read ) )
				for ( const auto &material : model->m_Materials )
					pending.push_back( "materials/" + material );
			else if ( !studio->m_Bodygroups.empty() )
				result.m_Notes.push_back( "No drawable mesh: " + path );
		}
		else if ( path.ends_with( ".vmt" ) )
		{
			const std::string text( reinterpret_cast<const char *>( bytes->data() ), bytes->size() );
			const auto parsed = kv1::Parse( text );
			for ( const auto &root : parsed.m_Document.m_Roots )
				if ( root.IsBlock() )
					if ( const auto *p = root.Find( "include" ); p && p->IsPair() )
					{
						auto include = vfs::NormalizePath( p->GetValue() );
						if ( !include.starts_with( "materials/" ) )
							include = "materials/" + include;
						if ( !include.ends_with( ".vmt" ) )
							include += ".vmt";
						pending.push_back( include );
					}
			if ( const auto material = material::ParseMaterial( text, read ) )
			{
				const auto textures = material::CTextureSet::Load( *material, read, 16, 1 );
				for ( const auto &texture : textures.GetTextures() )
					if ( !texture.m_szPath.empty() && texture.m_nKind == material::TextureKind_t::TEXTURE_KIND_FILE )
						pending.push_back( texture.m_szPath );
			}
		}
	}
	return result;
}
} // namespace xv::model
