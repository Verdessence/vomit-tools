//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Checked glTF 2.0 mesh input, following registry.khronos.org/glTF/specs/2.0/glTF-2.0.html.
//===========================================================================//
#include "ModelImport.h"
#include "model/Export.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Loads bounded local buffers; glTF input never retrieves remote resources.
//-----------------------------------------------------------------------------
QByteArray Read( const QString &szPath )
{
	QFile f( szPath );
	if ( !f.open( QIODevice::ReadOnly ) || f.size() > 256 * 1024 * 1024 )
		throw std::runtime_error( "Cannot read glTF input or buffer (256 MiB limit)" );
	return f.readAll();
}
//-----------------------------------------------------------------------------
// Purpose: Copies an aligned-independent little-endian scalar after a range check.
//-----------------------------------------------------------------------------
template <class T>
T Scalar( const QByteArray &bytes, qint64 nAt )
{
	if ( nAt < 0 || nAt > bytes.size() - static_cast<qint64>( sizeof( T ) ) )
		throw std::runtime_error( "glTF accessor runs outside its buffer" );
	T v;
	std::memcpy( &v, bytes.data() + nAt, sizeof( v ) );
	return v;
}
//-----------------------------------------------------------------------------
// Purpose: Reads and bounds a JSON array index or count without truncating non-integer inputs.
//-----------------------------------------------------------------------------
int Integer( const QJsonObject &object, const char *pszKey, int nDefault = 0 )
{
	if ( !object.contains( QLatin1String( pszKey ) ) )
		return nDefault;
	const double v = object.value( QLatin1String( pszKey ) ).toDouble( -1 );
	if ( !std::isfinite( v ) || v < 0 || v > 536870912 || std::floor( v ) != v )
		throw std::runtime_error( "Invalid glTF index or count" );
	return static_cast<int>( v );
}
//-----------------------------------------------------------------------------
// Purpose: Computes the node's specified matrix or its translation/rotation/scale composition.
//-----------------------------------------------------------------------------
QMatrix4x4 NodeMatrix( const QJsonObject &node )
{
	QMatrix4x4 m;
	const auto matrix = node.value( QStringLiteral( "matrix" ) ).toArray();
	if ( !matrix.empty() )
	{
		if ( matrix.size() != 16 )
			throw std::runtime_error( "Invalid glTF node matrix" );
		for ( int c = 0; c < 4; c++ )
			for ( int r = 0; r < 4; r++ )
				m( r, c ) = matrix[ c * 4 + r ].toDouble();
		return m;
	}
	const auto p = node.value( QStringLiteral( "translation" ) ).toArray(), q = node.value( QStringLiteral( "rotation" ) ).toArray(), s = node.value( QStringLiteral( "scale" ) ).toArray();
	if ( !p.empty() )
	{
		if ( p.size() != 3 )
			throw std::runtime_error( "Invalid glTF translation" );
		m.translate( p[ 0 ].toDouble(), p[ 1 ].toDouble(), p[ 2 ].toDouble() );
	}
	if ( !q.empty() )
	{
		if ( q.size() != 4 )
			throw std::runtime_error( "Invalid glTF rotation" );
		m.rotate( QQuaternion( q[ 3 ].toDouble(), q[ 0 ].toDouble(), q[ 1 ].toDouble(), q[ 2 ].toDouble() ).normalized() );
	}
	if ( !s.empty() )
	{
		if ( s.size() != 3 )
			throw std::runtime_error( "Invalid glTF scale" );
		m.scale( s[ 0 ].toDouble(), s[ 1 ].toDouble(), s[ 2 ].toDouble() );
	}
	return m;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Reads triangle primitives, maps skin joints by name and retains at most three weights.
//-----------------------------------------------------------------------------
xv::model::CModel ImportGltfMesh( const QString &szFile, const xv::model::CModel &target, int nRootBone )
{
	using namespace xv;
	auto bytes = Read( szFile );
	QByteArray binary;
	if ( bytes.startsWith( "glTF" ) )
	{
		if ( Scalar<std::uint32_t>( bytes, 4 ) != 2 || Scalar<std::uint32_t>( bytes, 8 ) != static_cast<std::uint32_t>( bytes.size() ) )
			throw std::runtime_error( "Invalid GLB header" );
		QByteArray json;
		for ( qint64 n = 12; n < bytes.size(); )
		{
			const auto size = Scalar<std::uint32_t>( bytes, n ), type = Scalar<std::uint32_t>( bytes, n + 4 );
			if ( size > static_cast<quint64>( bytes.size() - n - 8 ) )
				throw std::runtime_error( "GLB chunk runs outside its file" );
			if ( type == 0x4e4f534a )
				json = bytes.mid( n + 8, size );
			else if ( type == 0x004e4942 )
				binary = bytes.mid( n + 8, size );
			n += 8 + size;
		}
		bytes = json;
	}
	QJsonParseError error;
	const auto doc = QJsonDocument::fromJson( bytes, &error );
	if ( error.error != QJsonParseError::NoError || !doc.isObject() )
		throw std::runtime_error( "Invalid glTF JSON" );
	const auto root = doc.object();
	if ( root.value( QStringLiteral( "asset" ) ).toObject().value( QStringLiteral( "version" ) ).toString() != QStringLiteral( "2.0" ) )
		throw std::runtime_error( "Only glTF 2.0 is supported" );
	if ( !root.value( QStringLiteral( "extensionsRequired" ) ).toArray().empty() )
		throw std::runtime_error( "Convert required glTF extensions to ordinary triangle meshes before importing" );
	std::vector<QByteArray> buffers;
	qint64 nTotalBytes = 0;
	for ( const auto v : root.value( QStringLiteral( "buffers" ) ).toArray() )
	{
		const auto b = v.toObject();
		const auto uri = b.value( QStringLiteral( "uri" ) ).toString();
		QByteArray data;
		if ( uri.isEmpty() )
			data = binary;
		else if ( uri.startsWith( u"data:" ) )
		{
			const auto comma = uri.indexOf( u',' );
			if ( comma < 0 || !uri.left( comma ).endsWith( u";base64" ) )
				throw std::runtime_error( "Unsupported glTF data URI" );
			auto decoded = QByteArray::fromBase64Encoding( uri.mid( comma + 1 ).toLatin1(), QByteArray::AbortOnBase64DecodingErrors );
			if ( !decoded )
				throw std::runtime_error( "Invalid glTF base64 buffer" );
			data = decoded.decoded;
		}
		else
		{
			const auto path = QUrl::fromPercentEncoding( uri.toUtf8() );
			if ( !model::IsExportPath( path.toStdString() ) )
				throw std::runtime_error( "glTF buffer must be a local relative file" );
			data = Read( QDir( QFileInfo( szFile ).absolutePath() ).filePath( path ) );
		}
		if ( data.size() < Integer( b, "byteLength" ) )
			throw std::runtime_error( "Truncated glTF buffer" );
		nTotalBytes += data.size();
		if ( nTotalBytes > 512 * 1024 * 1024 )
			throw std::runtime_error( "glTF buffers exceed 512 MiB" );
		buffers.push_back( data );
	}
	const auto views = root.value( QStringLiteral( "bufferViews" ) ).toArray(), accessors = root.value( QStringLiteral( "accessors" ) ).toArray(), nodes = root.value( QStringLiteral( "nodes" ) ).toArray(), meshes = root.value( QStringLiteral( "meshes" ) ).toArray(), skins = root.value( QStringLiteral( "skins" ) ).toArray(), materials = root.value( QStringLiteral( "materials" ) ).toArray();
	const auto read = [ & ]( int nAccessor, int nExpected ) -> std::vector<std::array<float, 4>> {
		if ( nAccessor < 0 || nAccessor >= accessors.size() )
			throw std::runtime_error( "Invalid glTF accessor" );
		const auto a = accessors[ nAccessor ].toObject();
		if ( a.contains( QStringLiteral( "sparse" ) ) )
			throw std::runtime_error( "Flatten sparse glTF accessors before importing" );
		const auto type = a.value( QStringLiteral( "type" ) ).toString();
		const int nComponents = type == u"SCALAR" ? 1 : type == u"VEC2" ? 2
			: type == u"VEC3"                                           ? 3
			: type == u"VEC4"                                           ? 4
																		: 0;
		if ( nComponents != nExpected )
			throw std::runtime_error( "Unexpected glTF accessor type" );
		const int nView = Integer( a, "bufferView", -1 );
		if ( nView < 0 || nView >= views.size() )
			throw std::runtime_error( "Invalid glTF buffer view" );
		const auto view = views[ nView ].toObject();
		const int nBuffer = Integer( view, "buffer", -1 );
		if ( nBuffer < 0 || nBuffer >= static_cast<int>( buffers.size() ) )
			throw std::runtime_error( "Invalid glTF buffer index" );
		const int component = Integer( a, "componentType" ), size = component == 5126 || component == 5125 ? 4 : component == 5123 || component == 5122 ? 2
			: component == 5121 || component == 5120                                                                                                    ? 1
																																						: 0;
		if ( !size )
			throw std::runtime_error( "Unsupported glTF component type" );
		const int nCount = Integer( a, "count" ), nStart = Integer( a, "byteOffset" ), nViewStart = Integer( view, "byteOffset" ), nViewSize = Integer( view, "byteLength" ), nStride = Integer( view, "byteStride", nComponents * size );
		if ( nCount > 2000000 || nStride < nComponents * size || nViewStart > buffers[ nBuffer ].size() - nViewSize || nStart > nViewSize || ( nCount && static_cast<qint64>( nCount - 1 ) * nStride + nComponents * size > nViewSize - nStart ) )
			throw std::runtime_error( "Invalid glTF accessor bounds" );
		const bool normalized = a.value( QStringLiteral( "normalized" ) ).toBool();
		std::vector<std::array<float, 4>> out( nCount );
		for ( int i = 0; i < nCount; i++ )
			for ( int j = 0; j < nComponents; j++ )
			{
				const auto n = static_cast<qint64>( nViewStart ) + nStart + static_cast<qint64>( i ) * nStride + j * size;
				float f = 0;
				switch ( component )
				{
					case 5126: f = Scalar<float>( buffers[ nBuffer ], n ); break;
					case 5125: f = Scalar<std::uint32_t>( buffers[ nBuffer ], n ); break;
					case 5123:
						f = Scalar<std::uint16_t>( buffers[ nBuffer ], n );
						if ( normalized )
							f /= 65535;
						break;
					case 5122:
						f = Scalar<std::int16_t>( buffers[ nBuffer ], n );
						if ( normalized )
							f = std::max( -1.0f, f / 32767 );
						break;
					case 5121:
						f = Scalar<std::uint8_t>( buffers[ nBuffer ], n );
						if ( normalized )
							f /= 255;
						break;
					case 5120:
						f = Scalar<std::int8_t>( buffers[ nBuffer ], n );
						if ( normalized )
							f = std::max( -1.0f, f / 127 );
						break;
				}
				if ( !std::isfinite( f ) )
					throw std::runtime_error( "Non-finite glTF attribute" );
				out[ i ][ j ] = f;
			}
		return out;
	};
	if ( nodes.size() > 65536 )
		throw std::runtime_error( "Too many glTF nodes" );
	std::vector<int> parents( nodes.size(), -1 ), state( nodes.size() );
	std::vector<QMatrix4x4> world( nodes.size() );
	for ( int i = 0; i < nodes.size(); i++ )
		for ( const auto child : nodes[ i ].toObject().value( QStringLiteral( "children" ) ).toArray() )
		{
			const int c = child.toInt( -1 );
			if ( c < 0 || c >= nodes.size() || parents[ c ] != -1 )
				throw std::runtime_error( "Invalid glTF node hierarchy" );
			parents[ c ] = i;
		}
	const auto build = [ & ]( auto &&self, int i, int nDepth ) -> void {if(nDepth>1024||state[i]==1)throw std::runtime_error("Cyclic or excessively deep glTF hierarchy");if(state[i]==2)return;state[i]=1;if(parents[i]>=0)self(self,parents[i],nDepth+1);world[i]=parents[i]>=0?world[parents[i]]*NodeMatrix(nodes[i].toObject()):NodeMatrix(nodes[i].toObject());state[i]=2; };
	for ( int i = 0; i < nodes.size(); i++ )
		build( build, i, 0 );
	QMatrix4x4 source( 1 / 0.0254f, 0, 0, 0, 0, 0, -1 / 0.0254f, 0, 0, 1 / 0.0254f, 0, 0, 0, 0, 0, 1 );
	model::CModel out;
	out.m_Bones = target.m_Bones;
	if ( !out.m_Bones.empty() && ( nRootBone < 0 || nRootBone >= static_cast<int>( out.m_Bones.size() ) ) )
		throw std::runtime_error( "Invalid target attachment bone" );
	for ( int nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++ )
	{
		const auto node = nodes[ nodeIndex ].toObject();
		if ( !node.contains( QStringLiteral( "mesh" ) ) )
			continue;
		const int nMesh = Integer( node, "mesh" );
		if ( nMesh >= meshes.size() )
			throw std::runtime_error( "Invalid glTF mesh" );
		const auto matrix = source * world[ nodeIndex ];
		const int nSkin = Integer( node, "skin", -1 );
		QJsonArray skinJoints;
		if ( nSkin >= 0 )
		{
			if ( nSkin >= skins.size() )
				throw std::runtime_error( "Invalid glTF skin" );
			skinJoints = skins[ nSkin ].toObject().value( QStringLiteral( "joints" ) ).toArray();
		}
		for ( const auto value : meshes[ nMesh ].toObject().value( QStringLiteral( "primitives" ) ).toArray() )
		{
			const auto primitive = value.toObject();
			if ( Integer( primitive, "mode", 4 ) != 4 || primitive.contains( QStringLiteral( "targets" ) ) )
				throw std::runtime_error( "Import triangulated meshes without morph targets" );
			const auto attrs = primitive.value( QStringLiteral( "attributes" ) ).toObject();
			const auto positions = read( Integer( attrs, "POSITION", -1 ), 3 );
			std::vector<std::array<float, 4>> normals, uv, joints, weights;
			if ( attrs.contains( QStringLiteral( "NORMAL" ) ) )
				normals = read( Integer( attrs, "NORMAL" ), 3 );
			if ( attrs.contains( QStringLiteral( "TEXCOORD_0" ) ) )
				uv = read( Integer( attrs, "TEXCOORD_0" ), 2 );
			if ( attrs.contains( QStringLiteral( "JOINTS_0" ) ) )
			{
				joints = read( Integer( attrs, "JOINTS_0" ), 4 );
				weights = read( Integer( attrs, "WEIGHTS_0", -1 ), 4 );
			}
			for ( auto count : { normals.size(), uv.size(), joints.size(), weights.size() } )
				if ( count && count != positions.size() )
					throw std::runtime_error( "glTF attribute counts differ" );
			std::vector<std::array<float, 4>> indices;
			if ( primitive.contains( QStringLiteral( "indices" ) ) )
				indices = read( Integer( primitive, "indices" ), 1 );
			else
			{
				indices.resize( positions.size() );
				for ( std::size_t i = 0; i < indices.size(); i++ )
					indices[ i ][ 0 ] = i;
			}
			if ( indices.size() % 3 )
				throw std::runtime_error( "Invalid glTF triangle index count" );
			model::Mesh_t mesh;
			const auto nFirst = out.m_Vertices.size();
			const int material = Integer( primitive, "material", -1 );
			out.m_Materials.push_back( material >= 0 && material < materials.size() ? materials[ material ].toObject().value( QStringLiteral( "name" ) ).toString( QStringLiteral( "imported_material" ) ).toStdString() : "imported_material" );
			mesh.m_nMaterial = out.m_Materials.size() - 1;
			for ( std::size_t i = 0; i < positions.size(); i++ )
			{
				model::Vertex_t v;
				const auto p = matrix.map( QVector3D( positions[ i ][ 0 ], positions[ i ][ 1 ], positions[ i ][ 2 ] ) );
				v.m_vecPosition = { p.x(), p.y(), p.z() };
				if ( !normals.empty() )
				{
					const auto n = matrix.normalMatrix();
					const auto &a = normals[ i ];
					v.m_vecNormal = Normalized( { n( 0, 0 ) * a[ 0 ] + n( 0, 1 ) * a[ 1 ] + n( 0, 2 ) * a[ 2 ], n( 1, 0 ) * a[ 0 ] + n( 1, 1 ) * a[ 1 ] + n( 1, 2 ) * a[ 2 ], n( 2, 0 ) * a[ 0 ] + n( 2, 1 ) * a[ 1 ] + n( 2, 2 ) * a[ 2 ] } );
				}
				if ( !uv.empty() )
					v.m_Uv = { uv[ i ][ 0 ], uv[ i ][ 1 ] };
				if ( !joints.empty() )
					for ( int j = 0; j < 4; j++ )
						if ( weights[ i ][ j ] > 0.00001f )
						{
							if ( v.m_nBones == 3 )
								throw std::runtime_error( "Reduce glTF vertex influences to three before importing" );
							if ( joints[ i ][ j ] < 0 || std::floor( joints[ i ][ j ] ) != joints[ i ][ j ] )
								throw std::runtime_error( "Invalid glTF joint index" );
							const int joint = static_cast<int>( joints[ i ][ j ] );
							if ( joint < 0 || joint >= skinJoints.size() )
								throw std::runtime_error( "Invalid glTF joint" );
							const int id = skinJoints[ joint ].toInt( -1 );
							if ( id < 0 || id >= nodes.size() )
								throw std::runtime_error( "Invalid glTF joint node" );
							const auto name = nodes[ id ].toObject().value( QStringLiteral( "name" ) ).toString().toStdString();
							const auto found = std::ranges::find( out.m_Bones, name, &model::Bone_t::m_szName );
							if ( found == out.m_Bones.end() )
								throw std::runtime_error( "A glTF joint does not exist in the target skeleton: " + name );
							v.m_Bones[ v.m_nBones ] = found - out.m_Bones.begin();
							v.m_Weights[ v.m_nBones++ ] = weights[ i ][ j ];
						}
				if ( !out.m_Bones.empty() && !v.m_nBones )
				{
					v.m_nBones = 1;
					v.m_Bones[ 0 ] = nRootBone;
					v.m_Weights[ 0 ] = 1;
				}
				float total = 0;
				for ( int j = 0; j < v.m_nBones; j++ )
					total += v.m_Weights[ j ];
				if ( total > 0 )
					for ( int j = 0; j < v.m_nBones; j++ )
						v.m_Weights[ j ] /= total;
				out.m_Vertices.push_back( v );
			}
			for ( std::size_t i = 0; i < indices.size(); i += 3 )
				for ( int j : { 0, 2, 1 } )
				{
					const float f = indices[ i + j ][ 0 ];
					if ( f < 0 || std::floor( f ) != f || f >= positions.size() )
						throw std::runtime_error( "Invalid glTF vertex index" );
					mesh.m_Indices.push_back( nFirst + static_cast<std::size_t>( f ) );
				}
			if ( normals.empty() )
				for ( std::size_t i = 0; i < mesh.m_Indices.size(); i += 3 )
				{
					auto &a = out.m_Vertices[ mesh.m_Indices[ i ] ], &b = out.m_Vertices[ mesh.m_Indices[ i + 1 ] ], &c = out.m_Vertices[ mesh.m_Indices[ i + 2 ] ];
					const auto n = Normalized( Cross( c.m_vecPosition - a.m_vecPosition, b.m_vecPosition - a.m_vecPosition ) );
					a.m_vecNormal += n;
					b.m_vecNormal += n;
					c.m_vecNormal += n;
				}
			out.m_Meshes.push_back( std::move( mesh ) );
			if ( out.m_Vertices.size() > 2000000 )
				throw std::runtime_error( "Imported mesh exceeds the vertex limit" );
		}
	}
	for ( auto &v : out.m_Vertices )
		v.m_vecNormal = Normalized( v.m_vecNormal );
	if ( out.m_Meshes.empty() )
		throw std::runtime_error( "No triangle meshes in this glTF file" );
	return out;
}
