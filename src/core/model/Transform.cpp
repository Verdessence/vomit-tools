//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Transform.h. Layout facts are from the VDC MDL, VVD, VTX and PHY articles and the
//          public SDK headers, read for facts only.
//
//===========================================================================//
#include "Transform.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <numbers>
#include <set>
#include <stdexcept>

#include "Binary.h"

namespace xv::model
{

namespace
{

constexpr std::size_t BONE_SIZE = 216;
constexpr std::size_t HITBOX_SET_SIZE = 12;
constexpr std::size_t HITBOX_SIZE = 68;
constexpr std::size_t ANIMATION_SIZE = 100;
constexpr std::size_t SEQUENCE_SIZE = 212;
constexpr std::size_t BODYGROUP_SIZE = 16;
constexpr std::size_t MODEL_SIZE = 148;
constexpr std::size_t MESH_SIZE = 116;
constexpr std::size_t ATTACHMENT_SIZE = 92;
constexpr std::size_t CONTROLLER_SIZE = 56;
constexpr std::size_t EYEBALL_SIZE = 172;
constexpr std::size_t FLEX_SIZE = 60;
constexpr std::size_t MOVEMENT_SIZE = 44;
constexpr std::size_t IK_RULE_SIZE = 152;
constexpr std::size_t SOURCE_TRANSFORM_SIZE = 100;
constexpr std::size_t VVD_VERTEX_SIZE = 48;
constexpr std::size_t VVD_FIXUP_SIZE = 12;
constexpr std::size_t VTX_BODY_PART_SIZE = 8;
constexpr std::size_t VTX_MODEL_SIZE = 8;
constexpr std::size_t VTX_LOD_SIZE = 12;
constexpr std::size_t VTX_MESH_SIZE = 9;
constexpr std::size_t VTX_STRIP_GROUP_SIZE = 25;
constexpr std::size_t VTX_STRIP_SIZE = 27;
constexpr std::size_t VTX_VERTEX_SIZE = 9;
constexpr std::uint8_t STRIP_IS_TRISTRIP = 0x02;

constexpr int ANIMATION_DELTA = 0x0004;
constexpr int ANIMATION_ALL_ZEROS = 0x0020;
constexpr int ANIMATION_FRAME_BASED = 0x0040;
constexpr int TRACK_RAW_POSITION = 0x01;
constexpr int TRACK_RAW_ROTATION = 0x02;
constexpr int TRACK_RAW_ROTATION64 = 0x20;
constexpr int BONE_SAVED_POSITION = 0x00200000;
constexpr int BONE_SAVED_ROTATION = 0x00400000;
constexpr int VERTEX_ANIMATION_FIXED_POINT = 0x00200000;
constexpr int CONTROLLER_TRANSLATION = 0x0007; // moves along x, y or z rather than turning
constexpr float INCHES_PER_METRE = 1.0f / 0.0254f;

const std::array<std::string, 4> VTX_KINDS{ ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx" };

//-----------------------------------------------------------------------------
// Purpose: Checked writes beside CBinary's checked reads.
//-----------------------------------------------------------------------------
class CWriter
{
public:
	explicit CWriter( Bytes_t &bytes )
		: m_Bytes( bytes ),
		  m_View( bytes )
	{
	}
	[[nodiscard]] const CBinary &View() const { return m_View; }
	[[nodiscard]] int Int( std::size_t nAt ) const { return m_View.Int( nAt ); }
	[[nodiscard]] float Float( std::size_t nAt ) const { return m_View.Read<float>( nAt ); }
	[[nodiscard]] CVec3 Vector( std::size_t nAt ) const { return m_View.Vector( nAt ); }
	template <class T>
	void Put( std::size_t nAt, T value )
	{
		m_View.Check( nAt, sizeof( T ) );
		std::memcpy( m_Bytes.data() + nAt, &value, sizeof( T ) );
	}
	void PutFloat( std::size_t nAt, float flValue )
	{
		if ( !std::isfinite( flValue ) )
			throw std::runtime_error( "The change gives a number too large to store" );
		Put<float>( nAt, flValue );
	}
	void PutVector( std::size_t nAt, CVec3 vec )
	{
		PutFloat( nAt, vec.m_flX );
		PutFloat( nAt + 4, vec.m_flY );
		PutFloat( nAt + 8, vec.m_flZ );
	}
	// Stock files have NaNs in fields nothing reads; those are left as they are.
	[[nodiscard]] float RawFloat( std::size_t nAt ) const
	{
		m_View.Check( nAt, 4 );
		float flValue;
		std::memcpy( &flValue, m_Bytes.data() + nAt, 4 );
		return flValue;
	}
	[[nodiscard]] bool IsFinite( std::size_t nAt, std::size_t nCount = 3 ) const
	{
		for ( std::size_t i = 0; i < nCount; i++ )
			if ( !std::isfinite( RawFloat( nAt + i * 4 ) ) )
				return false;
		return true;
	}
	void Scale( std::size_t nAt, float flScale )
	{
		if ( const float flValue = RawFloat( nAt ); std::isfinite( flValue ) )
			PutFloat( nAt, flValue * flScale );
	}
	void ScaleVector( std::size_t nAt, float flScale )
	{
		for ( std::size_t i = 0; i < 3; i++ )
			Scale( nAt + i * 4, flScale );
	}
	void MapPoint( std::size_t nAt, const CTransform &transform )
	{
		if ( IsFinite( nAt ) )
			PutVector( nAt, transform.Apply( Vector( nAt ) ) );
	}
	// A direction turned (its length kept as a unit, or as it was with bKeepLength).
	void MapDirection( std::size_t nAt, const CTransform &transform, bool bKeepLength = false )
	{
		if ( !IsFinite( nAt ) )
			return;
		const CVec3 vec = Vector( nAt );
		PutVector( nAt, bKeepLength ? transform.Rotate( vec ) : Normalized( transform.Rotate( vec ) ) );
	}
	// The records of a list whose count is at nField and offset (from nBase) nGap bytes on.
	[[nodiscard]] std::vector<std::size_t> Records( std::size_t nBase, std::size_t nField, std::size_t nStride, std::size_t nGap = 4 ) const
	{
		const int nCount = Int( nField );
		std::vector<std::size_t> records;
		if ( nCount == 0 )
			return records;
		const auto nFirst = m_View.Relative( nBase, nField + nGap );
		m_View.Array( nFirst, nCount, nStride );
		for ( int i = 0; i < nCount; i++ )
			records.push_back( nFirst + static_cast<std::size_t>( i ) * nStride );
		return records;
	}
	[[nodiscard]] CTransform Matrix( std::size_t nAt ) const
	{
		CTransform m;
		for ( std::size_t i = 0; i < 12; i++ )
			m.m_Values[ i ] = Float( nAt + i * 4 );
		return m;
	}
	void PutMatrix( std::size_t nAt, const CTransform &m )
	{
		for ( std::size_t i = 0; i < 12; i++ )
			PutFloat( nAt + i * 4, m.m_Values[ i ] );
	}

private:
	Bytes_t &m_Bytes;
	CBinary m_View;
};

//-----------------------------------------------------------------------------
// Purpose: IEEE binary16, as raw animation positions and flex deltas are kept.
//-----------------------------------------------------------------------------
float HalfToFloat( std::uint16_t nBits )
{
	const int nExponent = ( nBits >> 10 ) & 0x1f, nMantissa = nBits & 0x3ff;
	const float flSign = nBits & 0x8000 ? -1.0f : 1.0f;
	if ( nExponent == 31 )
		throw std::runtime_error( "A half-precision value is not finite" );
	if ( nExponent == 0 )
		return flSign * std::ldexp( static_cast<float>( nMantissa ), -24 );
	return flSign * std::ldexp( static_cast<float>( nMantissa | 0x400 ), nExponent - 25 );
}

std::uint16_t FloatToHalf( float flValue )
{
	if ( !std::isfinite( flValue ) || std::abs( flValue ) > 65504.0f )
		throw std::runtime_error( "The change takes a value past what half precision holds (65504)" );
	const std::uint16_t nSign = std::signbit( flValue ) ? 0x8000 : 0;
	const float flAbs = std::abs( flValue );
	if ( flAbs < std::ldexp( 1.0f, -24 ) * 0.5f )
		return nSign;
	int nExponent;
	std::frexp( flAbs, &nExponent ); // flAbs = m * 2^e, m in [0.5, 1)
	int nBiased = nExponent - 1 + 15;
	if ( nBiased <= 0 )
		return static_cast<std::uint16_t>( nSign | static_cast<std::uint16_t>( std::lround( std::ldexp( flAbs, 24 ) ) ) );
	auto nMantissa = std::lround( std::ldexp( flAbs, 11 - nExponent ) ) - 0x400; // 10 bits after the leading one
	if ( nMantissa == 0x400 )
	{
		nMantissa = 0;
		nBiased++;
	}
	if ( nBiased >= 31 )
		throw std::runtime_error( "The change takes a value past what half precision holds (65504)" );
	return static_cast<std::uint16_t>( nSign | ( nBiased << 10 ) | static_cast<std::uint16_t>( nMantissa ) );
}

//-----------------------------------------------------------------------------
// Purpose: The corners of a box through a transform, boxed again (exact for quarter turns).
//-----------------------------------------------------------------------------
std::pair<CVec3, CVec3> TransformBox( CVec3 vecMin, CVec3 vecMax, const CTransform &transform )
{
	CVec3 lo{ FLT_MAX, FLT_MAX, FLT_MAX }, hi{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for ( int i = 0; i < 8; i++ )
	{
		const CVec3 p = transform.Apply( { i & 1 ? vecMax.m_flX : vecMin.m_flX, i & 2 ? vecMax.m_flY : vecMin.m_flY, i & 4 ? vecMax.m_flZ : vecMin.m_flZ } );
		lo = { std::min( lo.m_flX, p.m_flX ), std::min( lo.m_flY, p.m_flY ), std::min( lo.m_flZ, p.m_flZ ) };
		hi = { std::max( hi.m_flX, p.m_flX ), std::max( hi.m_flY, p.m_flY ), std::max( hi.m_flZ, p.m_flZ ) };
	}
	return { lo, hi };
}

//-----------------------------------------------------------------------------
// Purpose: A quarter-turn rotation as exact integers: roll about x, then pitch about y, then yaw
//          about z.
//-----------------------------------------------------------------------------
CTransform QuarterTurns( const Placement_t &placement )
{
	const auto turn = []( int nAxis, int nTurns ) {
		nTurns = ( ( nTurns % 4 ) + 4 ) % 4;
		const float c = nTurns == 0 ? 1.0f : nTurns == 2 ? -1.0f
														 : 0.0f;
		const float s = nTurns == 1 ? 1.0f : nTurns == 3 ? -1.0f
														 : 0.0f;
		CTransform m;
		if ( nAxis == 0 )
			m.m_Values = { 1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0 };
		else if ( nAxis == 1 )
			m.m_Values = { c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0 };
		else
			m.m_Values = { c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0 };
		return m;
	};
	// As CTransform::Rotation: a positive pitch (about y) looks down.
	CTransform m = turn( 2, placement.m_nYaw ) * turn( 1, placement.m_nPitch ) * turn( 0, placement.m_nRoll );
	m.m_Values[ 3 ] = placement.m_vecMove.m_flX;
	m.m_Values[ 7 ] = placement.m_vecMove.m_flY;
	m.m_Values[ 11 ] = placement.m_vecMove.m_flZ;
	return m;
}

//-----------------------------------------------------------------------------
// Purpose: What one whole-model edit does to each kind of thing:
//          - a model-space map for points kept in model space (eyes, bounds, vertices);
//          - a bone-space map for points kept in a bone's space (hitboxes, attachments, hulls);
//          - for scaling, the factor bones and animations take.
//-----------------------------------------------------------------------------
struct Mapping_t
{
	CTransform m_Model;
	CTransform m_Bone;
	float m_flScale = 1.0f; // 1 for a placement: the bones stay
	bool m_bScaling = false;
	float m_flRadiusGrowth = 0.0f; // what a model's bounding radius gains from a move
};

//-----------------------------------------------------------------------------
// Purpose: Every chain of animation tracks a model's animations have (in the .mdl or the .ani's
//          blocks), once each, as (file, offset); with the IK rules kept beside them.
//-----------------------------------------------------------------------------
struct AnimationData_t
{
	std::vector<std::pair<Bytes_t *, std::size_t>> m_Tracks;
	std::vector<std::pair<Bytes_t *, std::size_t>> m_IkRules;
	std::vector<std::size_t> m_ZeroFrames; // animation descriptions with saved frames
};
AnimationData_t FindAnimationData( Bytes_t &mdl, Bytes_t *pAni )
{
	CWriter w( mdl );
	AnimationData_t data;
	std::set<std::pair<Bytes_t *, std::size_t>> seen, seenRules;
	std::vector<std::pair<int, int>> blocks;
	for ( const auto n : w.Records( 0, 352, 8 ) )
		blocks.emplace_back( w.Int( n ), w.Int( n + 4 ) );
	const auto locate = [ & ]( std::size_t nDesc, int nBlock, int nIndex ) -> std::pair<Bytes_t *, std::size_t> {
		if ( nBlock < 0 || nIndex < 0 )
			throw std::runtime_error( "Invalid animation data offset" );
		if ( nBlock == 0 )
			return { &mdl, nDesc + static_cast<std::size_t>( nIndex ) };
		if ( !pAni )
			throw std::runtime_error( "The model's .ani (animation blocks) is missing" );
		if ( nBlock >= static_cast<int>( blocks.size() ) )
			throw std::runtime_error( "Animation block is out of range" );
		const auto [ nStart, nEnd ] = blocks[ static_cast<std::size_t>( nBlock ) ];
		if ( nStart < 0 || nEnd < nStart || static_cast<std::size_t>( nEnd ) > pAni->size() || nIndex > nEnd - nStart )
			throw std::runtime_error( "Invalid ANI block" );
		return { pAni, static_cast<std::size_t>( nStart + nIndex ) };
	};
	for ( const auto nDesc : w.Records( 0, 180, ANIMATION_SIZE ) )
	{
		const int nFlags = w.Int( nDesc + 12 ), nFrames = w.Int( nDesc + 16 );
		if ( w.Int( nDesc + 72 ) )
			throw std::runtime_error( "An animation has local hierarchy rules, which can't be changed in place yet" );
		if ( w.Int( nDesc + 92 ) && w.View().Read<std::int16_t>( nDesc + 90 ) > 0 )
			data.m_ZeroFrames.push_back( nDesc );
		// IK rules: in the .mdl, or in the animation's block.
		const int nRules = w.Int( nDesc + 60 );
		if ( nRules < 0 || nRules > 4096 )
			throw std::runtime_error( "Invalid IK rule count" );
		if ( nRules )
		{
			std::pair<Bytes_t *, std::size_t> first{ nullptr, 0 };
			if ( w.Int( nDesc + 64 ) )
				first = { &mdl, w.View().Relative( nDesc, nDesc + 64 ) };
			else if ( w.Int( nDesc + 68 ) )
				first = locate( nDesc, w.Int( nDesc + 52 ), w.Int( nDesc + 68 ) );
			if ( first.first )
				for ( int i = 0; i < nRules; i++ )
				{
					const std::pair<Bytes_t *, std::size_t> rule{ first.first, first.second + static_cast<std::size_t>( i ) * IK_RULE_SIZE };
					CBinary( *rule.first ).Check( rule.second, IK_RULE_SIZE );
					if ( seenRules.insert( rule ).second )
						data.m_IkRules.push_back( rule );
				}
		}
		if ( nFlags & ANIMATION_ALL_ZEROS )
			continue;
		std::vector<std::pair<int, int>> starts;
		const int nSectionFrames = w.Int( nDesc + 84 );
		if ( nSectionFrames < 0 || nFrames < 0 )
			throw std::runtime_error( "Invalid animation section duration" );
		if ( nSectionFrames > 0 )
		{
			const int nSections = nFrames / nSectionFrames + 2;
			const auto nAt = w.View().Relative( nDesc, nDesc + 80 );
			w.View().Array( nAt, nSections, 8 );
			for ( int i = 0; i < nSections; i++ )
				starts.emplace_back( w.Int( nAt + static_cast<std::size_t>( i ) * 8 ), w.Int( nAt + static_cast<std::size_t>( i ) * 8 + 4 ) );
		}
		else
			starts.emplace_back( w.Int( nDesc + 52 ), w.Int( nDesc + 56 ) );
		for ( const auto [ nBlock, nIndex ] : starts )
		{
			if ( !nIndex )
				continue;
			if ( nFlags & ANIMATION_FRAME_BASED )
				throw std::runtime_error( "An animation uses frame-based compression, which can't be changed in place yet" );
			const auto at = locate( nDesc, nBlock, nIndex );
			if ( seen.insert( at ).second )
				data.m_Tracks.push_back( at );
		}
	}
	return data;
}

//-----------------------------------------------------------------------------
// Purpose: Scales the raw positions in one chain of tracks; the compressed ones follow the bones'
//          position scales.
//-----------------------------------------------------------------------------
void ScaleTracks( Bytes_t &bytes, std::size_t nAt, int nBones, float flScale )
{
	CWriter w( bytes );
	for ( int nTrack = 0; nTrack <= nBones; nTrack++ )
	{
		const int nBone = w.View().Read<std::uint8_t>( nAt ), nFlags = w.View().Read<std::uint8_t>( nAt + 1 );
		if ( nBone == 255 )
			return;
		if ( nBone >= nBones )
			throw std::runtime_error( "An animation track names a bone the model doesn't have" );
		if ( nFlags & TRACK_RAW_POSITION )
		{
			const auto nPos = nAt + 4 + ( nFlags & TRACK_RAW_ROTATION ? 6 : 0 ) + ( nFlags & TRACK_RAW_ROTATION64 ? 8 : 0 );
			for ( std::size_t i = 0; i < 3; i++ )
				w.Put<std::uint16_t>( nPos + i * 2, FloatToHalf( HalfToFloat( w.View().Read<std::uint16_t>( nPos + i * 2 ) ) * flScale ) );
		}
		const auto nNext = w.View().Read<std::int16_t>( nAt + 2 );
		if ( !nNext )
			return;
		if ( nNext < 4 )
			throw std::runtime_error( "Invalid animation track link" );
		nAt += static_cast<std::size_t>( nNext );
	}
}

//-----------------------------------------------------------------------------
// Purpose: An IK rule's distances; its compressed error track's position scales.
//-----------------------------------------------------------------------------
void ScaleIkRule( Bytes_t &bytes, std::size_t n, float flScale )
{
	CWriter w( bytes );
	for ( const std::size_t nField : { 20, 24, 28, 96, 100 } )
		w.Scale( n + nField, flScale );
	w.ScaleVector( n + 32, flScale );
	if ( w.Int( n + 72 ) )
		throw std::runtime_error( "An IK rule has uncompressed errors, which can't be scaled in place yet" );
	if ( w.Int( n + 60 ) )
	{
		const auto nError = w.View().Relative( n, n + 60 );
		for ( std::size_t i = 0; i < 3; i++ )
			w.Scale( nError + i * 4, flScale );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The vertex's linear part (scale or turn), on a flex delta kept as half floats or, with
//          the fixed-point flag, as shorts times the header's scale.
//-----------------------------------------------------------------------------
void MapDelta( CWriter &w, std::size_t nAt, const CTransform &linear, bool bFixed, float flFixedScale, bool bNormalise )
{
	CVec3 d;
	if ( bFixed )
		d = CVec3{ static_cast<float>( w.View().Read<std::int16_t>( nAt ) ), static_cast<float>( w.View().Read<std::int16_t>( nAt + 2 ) ), static_cast<float>( w.View().Read<std::int16_t>( nAt + 4 ) ) } * flFixedScale;
	else
		d = { HalfToFloat( w.View().Read<std::uint16_t>( nAt ) ), HalfToFloat( w.View().Read<std::uint16_t>( nAt + 2 ) ), HalfToFloat( w.View().Read<std::uint16_t>( nAt + 4 ) ) };
	CVec3 out = linear.Rotate( d );
	if ( bNormalise )
		out = Normalized( out ) * std::sqrt( d.m_flX * d.m_flX + d.m_flY * d.m_flY + d.m_flZ * d.m_flZ );
	const float values[ 3 ] = { out.m_flX, out.m_flY, out.m_flZ };
	for ( std::size_t i = 0; i < 3; i++ )
	{
		if ( bFixed )
		{
			const long nValue = std::lround( values[ i ] / flFixedScale );
			if ( nValue < -32768 || nValue > 32767 )
				throw std::runtime_error( "A flex moves too far for its fixed-point scale" );
			w.Put<std::int16_t>( nAt + i * 2, static_cast<std::int16_t>( nValue ) );
		}
		else
			w.Put<std::uint16_t>( nAt + i * 2, FloatToHalf( values[ i ] ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Every raw vertex of the .vvd once, and its LOD 0 order (through the fixups).
//-----------------------------------------------------------------------------
struct VertexFile_t
{
	std::size_t m_nVertexStart = 0, m_nTangentStart = 0, m_nRaw = 0;
	std::vector<std::uint32_t> m_RootToRaw;
};
VertexFile_t ReadVertexFile( const Bytes_t &vvd, int nChecksum )
{
	const CBinary r( vvd );
	r.Check( 0, 64 );
	if ( r.Int( 0 ) != 0x56534449 || r.Int( 8 ) != nChecksum )
		throw std::runtime_error( "The .vvd doesn't belong to the .mdl" );
	VertexFile_t file;
	const int nRoot = r.Int( 16 ), nFixups = r.Int( 48 );
	if ( nRoot < 0 || nRoot > ( 1 << 22 ) || nFixups < 0 || nFixups > ( 1 << 20 ) )
		throw std::runtime_error( "Invalid .vvd counts" );
	file.m_nVertexStart = static_cast<std::size_t>( r.Int( 56 ) );
	file.m_nTangentStart = static_cast<std::size_t>( r.Int( 60 ) );
	file.m_nRaw = static_cast<std::size_t>( nRoot );
	if ( nFixups )
	{
		const auto nTable = static_cast<std::size_t>( r.Int( 52 ) );
		r.Array( nTable, nFixups, VVD_FIXUP_SIZE );
		for ( int i = 0; i < nFixups; i++ )
		{
			const auto n = nTable + static_cast<std::size_t>( i ) * VVD_FIXUP_SIZE;
			const int nLod = r.Int( n ), nSource = r.Int( n + 4 ), nCount = r.Int( n + 8 );
			if ( nSource < 0 || nCount < 0 || nCount > ( 1 << 22 ) )
				throw std::runtime_error( "Invalid .vvd fixup" );
			file.m_nRaw = std::max( file.m_nRaw, static_cast<std::size_t>( nSource ) + static_cast<std::size_t>( nCount ) );
			if ( nLod >= 0 )
				for ( int k = 0; k < nCount; k++ )
					file.m_RootToRaw.push_back( static_cast<std::uint32_t>( nSource + k ) );
		}
	}
	else
		for ( int i = 0; i < nRoot; i++ )
			file.m_RootToRaw.push_back( static_cast<std::uint32_t>( i ) );
	r.Array( file.m_nVertexStart, static_cast<int>( file.m_nRaw ), VVD_VERTEX_SIZE );
	if ( file.m_nTangentStart )
		r.Array( file.m_nTangentStart, static_cast<int>( file.m_nRaw ), 16 );
	return file;
}

//-----------------------------------------------------------------------------
// Purpose: The collision hulls: each solid's centre of mass, inertia, radius, its ledge tree's
//          spheres and its vertices, mapped in the solid's bone space and turned into the physics
//          engine's axes (metres, y and z swapped about).
//-----------------------------------------------------------------------------
void MapPhysics( Bytes_t &phy, const Mapping_t &mapping )
{
	CWriter w( phy );
	w.View().Check( 0, 16 );
	const int nSolids = w.Int( 8 );
	if ( nSolids < 0 || nSolids > 4096 || w.Int( 0 ) < 16 )
		throw std::runtime_error( "Invalid PHY header" );
	// Model axes (x, y, z) are the physics engine's (x, -z, y); K takes model to physics.
	CTransform k;
	k.m_Values = { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0 };
	CTransform kInverse;
	kInverse.m_Values = { 1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0 };
	// A solid's ledges name its bone; one that names none is in model space.
	const auto physicsMap = [ & ]( const CTransform &space ) {
		CTransform map = k * space * kInverse;
		for ( const int i : { 3, 7, 11 } )
			map.m_Values[ static_cast<std::size_t>( i ) ] /= INCHES_PER_METRE;
		return map;
	};
	const CTransform boneMap = physicsMap( mapping.m_Bone ), modelMap = physicsMap( mapping.m_Model );
	// For quarter turns, which physics axis each one comes from.
	const auto axesOf = [ & ]( const CTransform &map ) {
		std::array<int, 3> from{ 0, 1, 2 };
		for ( int row = 0; row < 3; row++ )
			for ( int col = 0; col < 3; col++ )
				if ( std::abs( map.m_Values[ static_cast<std::size_t>( row * 4 + col ) ] ) > 0.5f * mapping.m_flScale )
					from[ static_cast<std::size_t>( row ) ] = col;
		return from;
	};
	const float flScale = mapping.m_flScale;
	std::size_t nAt = static_cast<std::size_t>( w.Int( 0 ) );
	for ( int s = 0; s < nSolids; s++ )
	{
		const int nLength = w.Int( nAt );
		if ( nLength < 76 )
			throw std::runtime_error( "Invalid PHY solid length" );
		w.View().Check( nAt + 4, static_cast<std::size_t>( nLength ) );
		const bool bCompact = w.Int( nAt + 4 ) == 0x59485056;
		const std::size_t nSurface = nAt + ( bCompact ? 32 : 4 );
		// First the tree, to find the ledges' bone, then the writes.
		const std::size_t nRoot = nSurface + static_cast<std::size_t>( w.Int( nSurface + 32 ) );
		std::vector<std::size_t> pending{ nRoot };
		std::set<std::size_t> visited, vertices;
		bool bBoned = false;
		for ( std::size_t j = 0; j < pending.size(); j++ )
		{
			const auto nNode = pending[ j ];
			w.View().Check( nNode, 28 );
			if ( !visited.insert( nNode ).second || visited.size() > 65536 )
				throw std::runtime_error( "Cyclic or oversized PHY ledge tree" );
			if ( w.Int( nNode ) )
			{
				pending.push_back( nNode + 28 );
				pending.push_back( w.View().Relative( nNode, nNode ) );
				continue;
			}
			const auto nLedge = w.View().Relative( nNode, nNode + 4 );
			w.View().Check( nLedge, 16 );
			bBoned |= w.Int( nLedge + 4 ) > 0;
			const int nTriangles = w.View().Read<std::uint16_t>( nLedge + 12 );
			w.View().Array( nLedge + 16, nTriangles, 16 );
			std::uint32_t nCount = 0;
			for ( int t = 0; t < nTriangles; t++ )
				for ( int e = 0; e < 3; e++ )
					nCount = std::max<std::uint32_t>( nCount, w.View().Read<std::uint16_t>( nLedge + 16 + static_cast<std::size_t>( t ) * 16 + 4 + static_cast<std::size_t>( e ) * 4 ) + 1u );
			const auto nVertices = w.View().Relative( nLedge, nLedge );
			w.View().Array( nVertices, static_cast<int>( nCount ), 16 );
			for ( std::uint32_t v = 0; v < nCount; v++ )
				vertices.insert( nVertices + v * 16 );
		}
		const CTransform &map = bBoned ? boneMap : modelMap;
		const std::array<int, 3> from = axesOf( map );
		const auto permute = [ & ]( std::size_t nVec, float flFactor ) {
			if ( !w.IsFinite( nVec ) )
				return;
			const CVec3 v = w.Vector( nVec );
			const float in[ 3 ] = { v.m_flX, v.m_flY, v.m_flZ };
			w.PutVector( nVec, CVec3{ in[ from[ 0 ] ], in[ from[ 1 ] ], in[ from[ 2 ] ] } * flFactor );
		};
		if ( bCompact )
			permute( nAt + 16, flScale * flScale ); // drag areas
		w.MapPoint( nSurface, map );
		permute( nSurface + 12, flScale * flScale ); // rotation inertia
		w.Scale( nSurface + 24, flScale );
		// Inner nodes and leaves both have a sphere and a box (sizes per axis).
		for ( const auto nNode : visited )
		{
			w.MapPoint( nNode + 8, map );
			w.Scale( nNode + 20, flScale );
			std::array<std::uint8_t, 3> sizes{};
			for ( std::size_t a = 0; a < 3; a++ )
				sizes[ a ] = w.View().Read<std::uint8_t>( nNode + 24 + a );
			for ( std::size_t a = 0; a < 3; a++ )
				w.Put<std::uint8_t>( nNode + 24 + a, sizes[ static_cast<std::size_t>( from[ a ] ) ] );
		}
		for ( const auto nVertex : vertices )
			w.MapPoint( nVertex, map );
		nAt += static_cast<std::size_t>( nLength ) + 4;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The whole-model edit, shared by scaling and placing.
//-----------------------------------------------------------------------------
void MapModel( Files_t &files, const Mapping_t &mapping )
{
	const auto found = files.find( ".mdl" );
	if ( found == files.end() )
		throw std::runtime_error( "The model's .mdl is missing" );
	Files_t out = files;
	Bytes_t &mdl = out[ ".mdl" ];
	CWriter w( mdl );
	w.View().Check( 0, 408 );
	if ( w.Int( 0 ) != 0x54534449 || w.Int( 4 ) < 44 || w.Int( 4 ) > 49 || static_cast<std::size_t>( w.Int( 76 ) ) != mdl.size() )
		throw std::runtime_error( "Not a studio model this editor can change (versions 44–49)" );
	const int nChecksum = w.Int( 8 );
	const float s = mapping.m_flScale;
	const CTransform &model = mapping.m_Model;
	const CTransform &bone = mapping.m_Bone;
	CTransform linear = model;
	linear.m_Values[ 3 ] = linear.m_Values[ 7 ] = linear.m_Values[ 11 ] = 0;
	const auto mapBox = [ & ]( std::size_t nMin, const CTransform &transform ) {
		if ( !w.IsFinite( nMin, 6 ) )
			return;
		const auto [ lo, hi ] = TransformBox( w.Vector( nMin ), w.Vector( nMin + 12 ), transform );
		w.PutVector( nMin, lo );
		w.PutVector( nMin + 12, hi );
	};
	const auto scaleTranslation = [ & ]( std::size_t nMatrix ) {
		for ( const std::size_t i : { 3, 7, 11 } )
			w.Scale( nMatrix + i * 4, s );
	};

	// The header: eyes and light in model space, the boxes around the model.
	for ( const std::size_t nAt : { 80, 92 } )
		w.MapPoint( nAt, model );
	mapBox( 104, model );
	mapBox( 128, model );

	const int nBones = w.Int( 156 );
	if ( mapping.m_bScaling )
	{
		for ( const auto n : w.Records( 0, 156, BONE_SIZE ) )
		{
			w.ScaleVector( n + 32, s );
			w.ScaleVector( n + 72, s );
			scaleTranslation( n + 96 );
		}
		for ( const auto n : w.Records( 0, 164, CONTROLLER_SIZE ) )
			if ( w.Int( n + 4 ) & CONTROLLER_TRANSLATION )
			{
				w.Scale( n + 8, s );
				w.Scale( n + 12, s );
			}
		// The second header: bones' source transforms and the engine's linear copy of the bones.
		if ( const int nSecond = w.Int( 400 ) )
		{
			const auto h = static_cast<std::size_t>( nSecond );
			w.View().Check( h, 20 );
			for ( const auto n : w.Records( h, h, SOURCE_TRANSFORM_SIZE ) )
			{
				scaleTranslation( n + 4 );
				scaleTranslation( n + 52 );
			}
			if ( w.Int( h + 16 ) )
			{
				const auto l = w.View().Relative( h, h + 16 );
				w.View().Check( l, 40 );
				if ( w.Int( l ) != nBones )
					throw std::runtime_error( "The linear bone table doesn't match the bones" );
				const auto nPos = w.View().Relative( l, l + 12 ), nToBone = w.View().Relative( l, l + 24 ), nPosScale = w.View().Relative( l, l + 28 );
				w.View().Array( nPos, nBones, 12 );
				w.View().Array( nToBone, nBones, 48 );
				w.View().Array( nPosScale, nBones, 12 );
				for ( std::size_t i = 0; i < static_cast<std::size_t>( nBones ); i++ )
				{
					w.ScaleVector( nPos + i * 12, s );
					w.ScaleVector( nPosScale + i * 12, s );
					scaleTranslation( nToBone + i * 48 );
				}
			}
		}
	}

	// Hitboxes and attachments, in their bones' space.
	for ( const auto nSet : w.Records( 0, 172, HITBOX_SET_SIZE ) )
		for ( const auto n : w.Records( nSet, nSet + 4, HITBOX_SIZE ) )
			mapBox( n + 8, bone );
	// An attachment's turn stays a turn: the scale goes into where it is, not its axes.
	for ( const auto n : w.Records( 0, 240, ATTACHMENT_SIZE ) )
	{
		if ( !w.IsFinite( n + 12, 12 ) )
			continue;
		CTransform local = bone * w.Matrix( n + 12 );
		for ( const std::size_t i : { 0, 1, 2, 4, 5, 6, 8, 9, 10 } )
			local.m_Values[ i ] /= s;
		w.PutMatrix( n + 12, local );
	}

	// Animations: movement, IK rules and raw positions.
	Bytes_t *pAni = out.contains( ".ani" ) ? &out[ ".ani" ] : nullptr;
	for ( const auto nDesc : w.Records( 0, 180, ANIMATION_SIZE ) )
		for ( const auto n : w.Records( nDesc, nDesc + 20, MOVEMENT_SIZE ) )
		{
			for ( const std::size_t nField : { 8, 12 } )
				w.Scale( n + nField, s );
			w.MapDirection( n + 20, linear, true );
			w.MapDirection( n + 32, linear, true );
		}
	if ( mapping.m_bScaling )
	{
		const AnimationData_t animations = FindAnimationData( mdl, pAni );
		for ( const auto &[ pBytes, nAt ] : animations.m_Tracks )
			ScaleTracks( *pBytes, nAt, nBones, s );
		for ( const auto &[ pBytes, nAt ] : animations.m_IkRules )
			ScaleIkRule( *pBytes, nAt, s );
		// Saved first frames: per bone that has them, positions (half floats) then rotations.
		const auto bones = w.Records( 0, 156, BONE_SIZE );
		for ( const auto nDesc : animations.m_ZeroFrames )
		{
			const int nSaved = w.View().Read<std::int16_t>( nDesc + 90 );
			std::size_t nAt = w.View().Relative( nDesc, nDesc + 92 );
			for ( const auto nBone : bones )
			{
				const int nFlags = w.Int( nBone + 160 );
				if ( nFlags & BONE_SAVED_POSITION )
					for ( int f = 0; f < nSaved; f++, nAt += 6 )
						for ( std::size_t i = 0; i < 3; i++ )
							w.Put<std::uint16_t>( nAt + i * 2, FloatToHalf( HalfToFloat( w.View().Read<std::uint16_t>( nAt + i * 2 ) ) * s ) );
				if ( nFlags & BONE_SAVED_ROTATION )
					nAt += static_cast<std::size_t>( nSaved ) * 8;
			}
		}
	}
	for ( const auto n : w.Records( 0, 188, SEQUENCE_SIZE ) )
		mapBox( n + 32, model );

	// Bodygroup models: radius, meshes' centres, eyeballs and flexes.
	const bool bFixed = w.Int( 152 ) & VERTEX_ANIMATION_FIXED_POINT;
	const float flFixedScale = bFixed ? w.Float( 392 ) : 1.0f;
	if ( bFixed && mapping.m_bScaling )
		w.Scale( 392, s );
	for ( const auto nPart : w.Records( 0, 232, BODYGROUP_SIZE ) )
		for ( const auto nModel : w.Records( nPart, nPart + 4, MODEL_SIZE, 8 ) )
		{
			w.PutFloat( nModel + 68, w.Float( nModel + 68 ) * s + mapping.m_flRadiusGrowth );
			for ( const auto nEye : w.Records( nModel, nModel + 100, EYEBALL_SIZE ) )
			{
				w.MapPoint( nEye + 8, bone );
				w.Scale( nEye + 20, s );
				w.Scale( nEye + 24, s );
				w.MapDirection( nEye + 28, bone );
				w.MapDirection( nEye + 40, bone );
				w.Scale( nEye + 60, 1.0f / s );
			}
			for ( const auto nMesh : w.Records( nModel, nModel + 72, MESH_SIZE ) )
			{
				w.MapPoint( nMesh + 36, model );
				// Fixed-point deltas scale with the header's factor; half floats are rewritten.
				if ( bFixed && mapping.m_bScaling )
					continue;
				for ( const auto nFlex : w.Records( nMesh, nMesh + 16, FLEX_SIZE ) )
				{
					const std::size_t nStride = w.View().Read<std::uint8_t>( nFlex + 32 ) == 1 ? 18 : 16;
					for ( const auto nVertex : w.Records( nFlex, nFlex + 20, nStride ) )
					{
						MapDelta( w, nVertex + 4, linear, bFixed, flFixedScale, false );
						if ( !mapping.m_bScaling )
							MapDelta( w, nVertex + 10, linear, bFixed, flFixedScale, true );
					}
				}
			}
		}

	// The vertices, with their normals and tangents turned.
	if ( const auto vvd = out.find( ".vvd" ); vvd != out.end() )
	{
		const VertexFile_t layout = ReadVertexFile( vvd->second, nChecksum );
		CWriter v( vvd->second );
		for ( std::size_t i = 0; i < layout.m_nRaw; i++ )
		{
			const auto n = layout.m_nVertexStart + i * VVD_VERTEX_SIZE;
			v.MapPoint( n + 16, model );
			if ( mapping.m_bScaling )
				continue;
			v.MapDirection( n + 28, linear );
			if ( layout.m_nTangentStart )
			{
				const auto t = layout.m_nTangentStart + i * 16;
				v.MapDirection( t, linear );
			}
		}
	}
	if ( const auto phy = out.find( ".phy" ); phy != out.end() )
	{
		// Some stock hulls carry a stale checksum; they're the model's own and move with it.
		MapPhysics( phy->second, mapping );
	}
	files.swap( out );
}

//-----------------------------------------------------------------------------
// Purpose: Each mesh of a .vtx at one detail level, as (body part, model, mesh) and its offset.
//-----------------------------------------------------------------------------
struct VtxMesh_t
{
	int m_nPart = 0, m_nModel = 0, m_nLod = 0, m_nMesh = 0;
	std::size_t m_nAt = 0;
};
std::vector<VtxMesh_t> VtxMeshes( const Bytes_t &vtx, int nChecksum )
{
	const CBinary r( vtx );
	r.Check( 0, 36 );
	if ( r.Int( 0 ) != 7 || r.Int( 16 ) != nChecksum )
		throw std::runtime_error( "A .vtx doesn't belong to the .mdl" );
	std::vector<VtxMesh_t> meshes;
	const auto list = [ & ]( std::size_t nBase, std::size_t nField, std::size_t nStride ) {
		std::vector<std::size_t> records;
		const int nCount = r.Int( nField );
		if ( nCount <= 0 )
			return records;
		const auto nFirst = r.Relative( nBase, nField + 4 );
		r.Array( nFirst, nCount, nStride );
		for ( int i = 0; i < nCount; i++ )
			records.push_back( nFirst + static_cast<std::size_t>( i ) * nStride );
		return records;
	};
	const auto parts = list( 0, 28, VTX_BODY_PART_SIZE );
	for ( std::size_t p = 0; p < parts.size(); p++ )
	{
		const auto models = list( parts[ p ], parts[ p ], VTX_MODEL_SIZE );
		for ( std::size_t m = 0; m < models.size(); m++ )
		{
			const auto lods = list( models[ m ], models[ m ], VTX_LOD_SIZE );
			for ( std::size_t l = 0; l < lods.size(); l++ )
			{
				const auto list2 = list( lods[ l ], lods[ l ], VTX_MESH_SIZE );
				for ( std::size_t k = 0; k < list2.size(); k++ )
					meshes.push_back( { static_cast<int>( p ), static_cast<int>( m ), static_cast<int>( l ), static_cast<int>( k ), list2[ k ] } );
			}
		}
	}
	return meshes;
}

int Checksum( const Files_t &files )
{
	const auto found = files.find( ".mdl" );
	if ( found == files.end() )
		throw std::runtime_error( "The model's .mdl is missing" );
	const CBinary r( found->second );
	r.Check( 0, 408 );
	return r.Int( 8 );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void ScaleModel( Files_t &files, float flScale )
{
	if ( !( flScale > 0.001f ) || flScale > 1000.0f )
		throw std::runtime_error( "The scale is between 0.001 and 1000" );
	Mapping_t mapping;
	mapping.m_Model.m_Values = { flScale, 0, 0, 0, 0, flScale, 0, 0, 0, 0, flScale, 0 };
	mapping.m_Bone = mapping.m_Model;
	mapping.m_flScale = flScale;
	mapping.m_bScaling = true;
	MapModel( files, mapping );
}

//-----------------------------------------------------------------------------
// Purpose: One bone, so nothing animates the turn away; IK and controllers would point elsewhere.
//-----------------------------------------------------------------------------
std::string CanPlace( const Files_t &files )
{
	try
	{
		Bytes_t mdl = files.at( ".mdl" );
		CWriter w( mdl );
		w.View().Check( 0, 408 );
		if ( w.Int( 156 ) != 1 )
			return "Turning and moving in place is for models with one bone (props); this one has " + std::to_string( w.Int( 156 ) ) + ", and its animations would need recompiling.";
		if ( w.Int( 164 ) )
			return "The model has bone controllers, which would point the old way.";
		for ( const auto nDesc : w.Records( 0, 180, ANIMATION_SIZE ) )
			if ( w.Int( nDesc + 60 ) )
				return "The model's animations have IK rules, which would point the old way.";
		if ( const int nSecond = w.Int( 400 ) )
			if ( w.Int( static_cast<std::size_t>( nSecond ) ) )
				return "The model has source bone transforms, which would point the old way.";
		return {};
	}
	catch ( const std::exception &e )
	{
		return e.what();
	}
}

//-----------------------------------------------------------------------------
// Purpose: In the bone's space the placement is P·T·B (B the bone's reference pose, P its
//          inverse), so whatever the bone carries lands where the model-space map puts it.
//-----------------------------------------------------------------------------
void PlaceModel( Files_t &files, const Placement_t &placement )
{
	if ( const std::string szWhy = CanPlace( files ); !szWhy.empty() )
		throw std::runtime_error( szWhy );
	Bytes_t mdl = files.at( ".mdl" );
	CWriter w( mdl );
	const auto nBone = w.Records( 0, 156, BONE_SIZE ).front();
	const CTransform poseToBone = w.Matrix( nBone + 96 );
	const auto boneToPose = poseToBone.Inverse();
	if ( !boneToPose )
		throw std::runtime_error( "The bone's reference pose can't be undone" );
	Mapping_t mapping;
	mapping.m_Model = QuarterTurns( placement );
	mapping.m_Bone = poseToBone * mapping.m_Model * *boneToPose;
	// Boxes and hull spheres stay exact only when the turn is still quarter turns in the bone's space.
	for ( const std::size_t i : { 0, 1, 2, 4, 5, 6, 8, 9, 10 } )
	{
		const float flValue = std::abs( mapping.m_Bone.m_Values[ i ] );
		if ( std::min( flValue, std::abs( flValue - 1.0f ) ) > 1e-4f )
			throw std::runtime_error( "The model's bone isn't lined up with its axes, so a quarter turn wouldn't be one in its space" );
	}
	const CVec3 &move = placement.m_vecMove;
	mapping.m_flRadiusGrowth = std::sqrt( move.m_flX * move.m_flX + move.m_flY * move.m_flY + move.m_flZ * move.m_flZ );
	MapModel( files, mapping );
}

//-----------------------------------------------------------------------------
// Purpose: A mesh with no strip groups draws nothing at that level.
//-----------------------------------------------------------------------------
void RemoveMesh( Files_t &files, int nBodygroup, int nModel, int nMesh )
{
	const int nChecksum = Checksum( files );
	Files_t out = files;
	bool bFound = false;
	for ( const auto &szKind : VTX_KINDS )
	{
		const auto vtx = out.find( szKind );
		if ( vtx == out.end() )
			continue;
		for ( const auto &mesh : VtxMeshes( vtx->second, nChecksum ) )
			if ( mesh.m_nPart == nBodygroup && mesh.m_nModel == nModel && mesh.m_nMesh == nMesh )
			{
				CWriter( vtx->second ).Put<std::int32_t>( mesh.m_nAt, 0 );
				bFound = true;
			}
	}
	if ( !bFound )
		throw std::runtime_error( "No .vtx has that mesh" );
	files.swap( out );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<float> ReadLodSwitchPoints( const Files_t &files )
{
	for ( const auto &szKind : VTX_KINDS )
	{
		const auto vtx = files.find( szKind );
		if ( vtx == files.end() )
			continue;
		const CBinary r( vtx->second );
		r.Check( 0, 36 );
		const int nParts = r.Int( 28 );
		if ( nParts <= 0 )
			return {};
		const auto nPart = r.Relative( 0, 32 );
		r.Array( nPart, nParts, VTX_BODY_PART_SIZE );
		for ( int p = 0; p < nParts; p++ )
		{
			const auto nPartAt = nPart + static_cast<std::size_t>( p ) * VTX_BODY_PART_SIZE;
			const int nModels = r.Int( nPartAt );
			if ( nModels <= 0 )
				continue;
			const auto nModel = r.Relative( nPartAt, nPartAt + 4 );
			r.Array( nModel, nModels, VTX_MODEL_SIZE );
			const int nLods = r.Int( nModel );
			if ( nLods <= 0 || nLods > 8 )
				continue;
			const auto nLod = r.Relative( nModel, nModel + 4 );
			r.Array( nLod, nLods, VTX_LOD_SIZE );
			std::vector<float> points;
			for ( int l = 0; l < nLods; l++ )
				points.push_back( r.Read<float>( nLod + static_cast<std::size_t>( l ) * VTX_LOD_SIZE + 8 ) );
			return points;
		}
		return {};
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The engine stops at the first level whose switch point is past the model's distance,
//          so an unreachable point there also keeps every later level away.
//-----------------------------------------------------------------------------
void LimitLods( Files_t &files, int nKeep )
{
	if ( nKeep < 1 )
		throw std::runtime_error( "At least the first detail level stays" );
	const int nChecksum = Checksum( files );
	Files_t out = files;
	for ( const auto &szKind : VTX_KINDS )
	{
		const auto vtx = out.find( szKind );
		if ( vtx == out.end() )
			continue;
		static_cast<void>( VtxMeshes( vtx->second, nChecksum ) ); // checks every level's records
		CWriter w( vtx->second );
		const CBinary &r = w.View();
		const int nParts = r.Int( 28 );
		const auto nPart = nParts > 0 ? r.Relative( 0, 32 ) : 0;
		for ( int p = 0; p < nParts; p++ )
		{
			const auto nPartAt = nPart + static_cast<std::size_t>( p ) * VTX_BODY_PART_SIZE;
			const int nModels = r.Int( nPartAt );
			const auto nModel = nModels > 0 ? r.Relative( nPartAt, nPartAt + 4 ) : 0;
			for ( int m = 0; m < nModels; m++ )
			{
				const auto nModelAt = nModel + static_cast<std::size_t>( m ) * VTX_MODEL_SIZE;
				const int nLods = r.Int( nModelAt );
				if ( nLods <= 0 )
					continue;
				const auto nLod = r.Relative( nModelAt, nModelAt + 4 );
				r.Array( nLod, nLods, VTX_LOD_SIZE );
				for ( int l = nKeep; l < nLods; l++ )
				{
					const auto nPoint = nLod + static_cast<std::size_t>( l ) * VTX_LOD_SIZE + 8;
					if ( w.Float( nPoint ) >= 0 )
						w.PutFloat( nPoint, FLT_MAX );
				}
			}
		}
	}
	files.swap( out );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void RecomputeNormals( Files_t &files, float flHardAngle )
{
	if ( !( flHardAngle >= 0 && flHardAngle <= 180 ) )
		throw std::runtime_error( "The hard-edge angle is between 0 and 180 degrees" );
	const int nChecksum = Checksum( files );
	Files_t out = files;
	Bytes_t &mdl = out.at( ".mdl" );
	if ( !out.contains( ".vvd" ) )
		throw std::runtime_error( "The model's .vvd is missing" );
	Bytes_t &vvd = out.at( ".vvd" );
	const VertexFile_t layout = ReadVertexFile( vvd, nChecksum );
	CWriter m( mdl ), v( vvd );

	// The root level's triangles, from the first .vtx there is (they all have the same ones).
	const Bytes_t *pVtx = nullptr;
	for ( const auto &szKind : VTX_KINDS )
		if ( out.contains( szKind ) )
		{
			pVtx = &out.at( szKind );
			break;
		}
	if ( !pVtx )
		throw std::runtime_error( "The model's .vtx is missing" );
	const CBinary r( *pVtx );
	std::vector<std::array<std::uint32_t, 3>> triangles; // raw vertex indices
	const auto parts = m.Records( 0, 232, BODYGROUP_SIZE );
	for ( const auto &mesh : VtxMeshes( *pVtx, nChecksum ) )
	{
		if ( mesh.m_nLod != 0 || mesh.m_nPart >= static_cast<int>( parts.size() ) )
			continue;
		const auto models = m.Records( parts[ static_cast<std::size_t>( mesh.m_nPart ) ], parts[ static_cast<std::size_t>( mesh.m_nPart ) ] + 4, MODEL_SIZE, 8 );
		if ( mesh.m_nModel >= static_cast<int>( models.size() ) )
			continue;
		const auto nModelAt = models[ static_cast<std::size_t>( mesh.m_nModel ) ];
		const auto meshes = m.Records( nModelAt, nModelAt + 72, MESH_SIZE );
		if ( mesh.m_nMesh >= static_cast<int>( meshes.size() ) )
			continue;
		const std::size_t nFirst = static_cast<std::size_t>( std::max( 0, m.Int( nModelAt + 84 ) ) ) / VVD_VERTEX_SIZE + static_cast<std::size_t>( std::max( 0, m.Int( meshes[ static_cast<std::size_t>( mesh.m_nMesh ) ] + 12 ) ) );
		const int nGroups = r.Int( mesh.m_nAt );
		if ( nGroups <= 0 )
			continue;
		const auto nGroup = r.Relative( mesh.m_nAt, mesh.m_nAt + 4 );
		r.Array( nGroup, nGroups, VTX_STRIP_GROUP_SIZE );
		for ( int g = 0; g < nGroups; g++ )
		{
			const auto n = nGroup + static_cast<std::size_t>( g ) * VTX_STRIP_GROUP_SIZE;
			const int nVerts = r.Int( n ), nIndices = r.Int( n + 8 ), nStrips = r.Int( n + 16 );
			const auto nVert = r.Relative( n, n + 4 ), nIndex = r.Relative( n, n + 12 ), nStrip = r.Relative( n, n + 20 );
			r.Array( nVert, nVerts, VTX_VERTEX_SIZE );
			r.Array( nIndex, nIndices, 2 );
			r.Array( nStrip, nStrips, VTX_STRIP_SIZE );
			const auto raw = [ & ]( int nAt ) -> std::optional<std::uint32_t> {
				const int nLocal = r.Read<std::uint16_t>( nIndex + static_cast<std::size_t>( nAt ) * 2 );
				if ( nLocal >= nVerts )
					return std::nullopt;
				const std::size_t nRoot = nFirst + r.Read<std::uint16_t>( nVert + static_cast<std::size_t>( nLocal ) * VTX_VERTEX_SIZE + 4 );
				if ( nRoot >= layout.m_RootToRaw.size() )
					return std::nullopt;
				return layout.m_RootToRaw[ nRoot ];
			};
			const auto add = [ & ]( int a, int b, int c ) {
				const auto va = raw( a ), vb = raw( b ), vc = raw( c );
				if ( va && vb && vc && *va != *vb && *vb != *vc && *va != *vc )
					triangles.push_back( { *va, *vb, *vc } );
			};
			for ( int t = 0; t < nStrips; t++ )
			{
				const auto s = nStrip + static_cast<std::size_t>( t ) * VTX_STRIP_SIZE;
				const int nCount = r.Int( s ), nStart = r.Int( s + 4 );
				if ( nCount < 0 || nStart < 0 || nStart > nIndices - nCount )
					continue;
				if ( r.Read<std::uint8_t>( s + 18 ) & STRIP_IS_TRISTRIP )
				{
					for ( int i = 0; i + 2 < nCount; i++ )
						if ( i % 2 == 0 )
							add( nStart + i, nStart + i + 1, nStart + i + 2 );
						else
							add( nStart + i + 1, nStart + i, nStart + i + 2 );
				}
				else
					for ( int i = 0; i + 2 < nCount; i += 3 )
						add( nStart + i, nStart + i + 1, nStart + i + 2 );
			}
		}
	}
	if ( triangles.empty() )
		throw std::runtime_error( "The model has no triangles to work normals out from" );

	const auto position = [ & ]( std::uint32_t i ) { return v.Vector( layout.m_nVertexStart + i * VVD_VERTEX_SIZE + 16 ); };
	const auto uv = [ & ]( std::uint32_t i ) { const auto n = layout.m_nVertexStart + i * VVD_VERTEX_SIZE + 40; return std::array<float, 2>{ v.Float( n ), v.Float( n + 4 ) }; };
	const auto cross = []( CVec3 a, CVec3 b ) { return CVec3{ a.m_flY * b.m_flZ - a.m_flZ * b.m_flY, a.m_flZ * b.m_flX - a.m_flX * b.m_flZ, a.m_flX * b.m_flY - a.m_flY * b.m_flX }; };
	const auto dot = []( CVec3 a, CVec3 b ) { return a.m_flX * b.m_flX + a.m_flY * b.m_flY + a.m_flZ * b.m_flZ; };

	// Faces by the vertices they use, and vertices by where they are.
	std::vector<CVec3> faceNormals;
	std::vector<std::vector<std::uint32_t>> facesOf( layout.m_nRaw );
	for ( std::uint32_t f = 0; f < triangles.size(); f++ )
	{
		const auto &[ a, b, c ] = triangles[ f ];
		faceNormals.push_back( cross( position( c ) - position( a ), position( b ) - position( a ) ) ); // area-weighted; Source winds faces clockwise
		for ( const auto i : triangles[ f ] )
			facesOf[ i ].push_back( f );
	}
	std::map<std::array<std::int64_t, 3>, std::vector<std::uint32_t>> at;
	const auto key = [ & ]( std::uint32_t i ) {
		const CVec3 p = position( i );
		return std::array<std::int64_t, 3>{ std::llround( p.m_flX * 1024.0 ), std::llround( p.m_flY * 1024.0 ), std::llround( p.m_flZ * 1024.0 ) };
	};
	for ( std::uint32_t i = 0; i < layout.m_nRaw; i++ )
		if ( !facesOf[ i ].empty() )
			at[ key( i ) ].push_back( i );
	const float flCos = std::cos( flHardAngle * std::numbers::pi_v<float> / 180.0f );
	std::vector<CVec3> normals( layout.m_nRaw );
	for ( std::uint32_t i = 0; i < layout.m_nRaw; i++ )
	{
		if ( facesOf[ i ].empty() )
			continue;
		CVec3 own;
		for ( const auto f : facesOf[ i ] )
			own += faceNormals[ f ];
		own = Normalized( own );
		CVec3 sum;
		std::set<std::uint32_t> used;
		for ( const auto j : at[ key( i ) ] )
			for ( const auto f : facesOf[ j ] )
				if ( used.insert( f ).second && dot( Normalized( faceNormals[ f ] ), own ) >= flCos - 1e-5f )
					sum += faceNormals[ f ];
		normals[ i ] = Normalized( sum );
		if ( dot( normals[ i ], normals[ i ] ) < 0.5f )
			normals[ i ] = own;
	}

	// Tangents along increasing u, with the handedness the bitangent (increasing v) gives.
	std::vector<CVec3> tangents( layout.m_nRaw ), bitangents( layout.m_nRaw );
	for ( const auto &[ a, b, c ] : triangles )
	{
		const CVec3 e1 = position( b ) - position( a ), e2 = position( c ) - position( a );
		const auto ta = uv( a ), tb = uv( b ), tc = uv( c );
		const float du1 = tb[ 0 ] - ta[ 0 ], dv1 = tb[ 1 ] - ta[ 1 ], du2 = tc[ 0 ] - ta[ 0 ], dv2 = tc[ 1 ] - ta[ 1 ];
		const float flDet = du1 * dv2 - du2 * dv1;
		if ( std::abs( flDet ) < 1e-12f )
			continue;
		const float flInverse = 1.0f / flDet;
		const CVec3 t = ( e1 * dv2 - e2 * dv1 ) * flInverse;
		const CVec3 bt = ( e2 * du1 - e1 * du2 ) * flInverse;
		for ( const auto i : { a, b, c } )
		{
			tangents[ i ] += t;
			bitangents[ i ] += bt;
		}
	}
	for ( std::uint32_t i = 0; i < layout.m_nRaw; i++ )
	{
		if ( facesOf[ i ].empty() )
			continue;
		const auto n = layout.m_nVertexStart + i * VVD_VERTEX_SIZE;
		v.PutVector( n + 28, normals[ i ] );
		if ( !layout.m_nTangentStart )
			continue;
		CVec3 t = tangents[ i ] - normals[ i ] * dot( normals[ i ], tangents[ i ] );
		if ( dot( t, t ) < 1e-12f )
		{
			// No UV direction: any direction across the normal.
			t = std::abs( normals[ i ].m_flX ) < 0.9f ? cross( normals[ i ], { 1, 0, 0 } ) : cross( normals[ i ], { 0, 1, 0 } );
		}
		t = Normalized( t );
		const float flW = dot( cross( normals[ i ], t ), bitangents[ i ] ) < 0 ? -1.0f : 1.0f;
		const auto nTangent = layout.m_nTangentStart + i * 16;
		v.PutVector( nTangent, t );
		v.PutFloat( nTangent + 12, flW );
	}
	files.swap( out );
}

} // namespace xv::model
