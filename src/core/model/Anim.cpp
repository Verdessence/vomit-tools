//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Decode format fields independently of Valve's implementation; see Anim.h.
//===========================================================================//
#include "Anim.h"
#include "Binary.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
namespace xv::model
{
namespace
{
using Quaternion_t = std::array<float, 4>;
struct Pose_t
{
	CVec3 m_vecPosition;
	Quaternion_t m_Quaternion{ 0, 0, 0, 1 };
};
//-----------------------------------------------------------------------------
// Purpose: Shortest-arc interpolation of unit rotations.
//-----------------------------------------------------------------------------
Quaternion_t BlendRotation( Quaternion_t a, Quaternion_t b, float flWeight )
{
	float flDot = 0;
	for ( int i = 0; i < 4; i++ )
		flDot += a[ i ] * b[ i ];
	if ( flDot < 0 )
	{
		for ( float &flValue : b )
			flValue = -flValue;
		flDot = -flDot;
	}
	float flA = 1 - flWeight, flB = flWeight;
	if ( flDot < 0.9995f )
	{
		const float flAngle = std::acos( std::clamp( flDot, 0.0f, 1.0f ) );
		flA = std::sin( ( 1 - flWeight ) * flAngle ) / std::sin( flAngle );
		flB = std::sin( flWeight * flAngle ) / std::sin( flAngle );
	}
	float flLength = 0;
	for ( int i = 0; i < 4; i++ )
	{
		a[ i ] = a[ i ] * flA + b[ i ] * flB;
		flLength += a[ i ] * a[ i ];
	}
	if ( flLength < 1e-12f )
		return { 0, 0, 0, 1 };
	for ( float &flValue : a )
		flValue /= std::sqrt( flLength );
	return a;
}
//-----------------------------------------------------------------------------
// Purpose: Converts a local pose to the affine transform used by the previews.
//-----------------------------------------------------------------------------
CTransform Transform( const Pose_t &pose )
{
	const auto &[ x, y, z, w ] = pose.m_Quaternion;
	CTransform out;
	out.m_Values = { 1 - 2 * ( y * y + z * z ), 2 * ( x * y - z * w ), 2 * ( x * z + y * w ), pose.m_vecPosition.m_flX,
		2 * ( x * y + z * w ), 1 - 2 * ( x * x + z * z ), 2 * ( y * z - x * w ), pose.m_vecPosition.m_flY,
		2 * ( x * z - y * w ), 2 * ( y * z + x * w ), 1 - 2 * ( x * x + y * y ), pose.m_vecPosition.m_flZ };
	return out;
}
//-----------------------------------------------------------------------------
// Purpose: Converts XYZ Euler rotations in radians to a normalized quaternion.
//-----------------------------------------------------------------------------
Quaternion_t Rotation( CVec3 vecAngles )
{
	const float x = vecAngles.m_flX * 0.5f, y = vecAngles.m_flY * 0.5f, z = vecAngles.m_flZ * 0.5f;
	const float a = std::sin( x ), b = std::sin( y ), c = std::sin( z ), d = std::cos( x ), e = std::cos( y ), f = std::cos( z );
	return { a * e * f - d * b * c, d * b * f + a * e * c, d * e * c - a * b * f, d * e * f + a * b * c };
}
//-----------------------------------------------------------------------------
// Purpose: Unpacks IEEE binary16 positions, rejecting infinity and NaN.
//-----------------------------------------------------------------------------
float Half( std::uint16_t nBits )
{
	const int nExponent = ( nBits >> 10 ) & 31;
	if ( nExponent == 31 )
		throw std::runtime_error( "Non-finite packed animation position" );
	const float flSign = nBits & 0x8000 ? -1.0f : 1.0f;
	return flSign * ( nExponent ? std::ldexp( 1.0f + ( nBits & 1023 ) / 1024.0f, nExponent - 15 ) : std::ldexp( static_cast<float>( nBits & 1023 ), -24 ) );
}
//-----------------------------------------------------------------------------
// Purpose: Unpacks the three stored quaternion components and the sign of the fourth.
//-----------------------------------------------------------------------------
Quaternion_t PackedRotation( const CBinary &r, std::size_t nAt, bool b64 )
{
	Quaternion_t q{};
	bool bNegative;
	if ( b64 )
	{
		const auto n = r.Read<std::uint64_t>( nAt );
		for ( int i = 0; i < 3; i++ )
			q[ i ] = ( static_cast<int>( ( n >> ( i * 21 ) ) & 0x1fffff ) - 1048576 ) / 1048576.5f;
		bNegative = ( n >> 63 ) != 0;
	}
	else
	{
		q[ 0 ] = ( static_cast<int>( r.Read<std::uint16_t>( nAt ) ) - 32768 ) / 32768.0f;
		q[ 1 ] = ( static_cast<int>( r.Read<std::uint16_t>( nAt + 2 ) ) - 32768 ) / 32768.0f;
		const auto n = r.Read<std::uint16_t>( nAt + 4 );
		q[ 2 ] = ( ( n & 32767 ) - 16384 ) / 16384.0f;
		bNegative = n & 32768;
	}
	q[ 3 ] = std::sqrt( std::max( 0.0f, 1 - q[ 0 ] * q[ 0 ] - q[ 1 ] * q[ 1 ] - q[ 2 ] * q[ 2 ] ) ) * ( bNegative ? -1.0f : 1.0f );
	return BlendRotation( q, q, 0 );
}
//-----------------------------------------------------------------------------
// Purpose: Reads one RLE channel; a run repeats its last valid value for the remaining frames.
//-----------------------------------------------------------------------------
float Channel( const CBinary &r, std::size_t nPointer, int nAxis, int nFrame )
{
	const auto nRelative = r.Read<std::int16_t>( nPointer + nAxis * 2 );
	if ( !nRelative )
		return 0;
	if ( nRelative < 0 )
		throw std::runtime_error( "Negative animation channel offset" );
	std::size_t nAt = nPointer + nRelative;
	while ( nFrame >= 0 )
	{
		const int nValid = r.Read<std::uint8_t>( nAt ), nTotal = r.Read<std::uint8_t>( nAt + 1 );
		if ( nValid < 1 || nTotal < nValid )
			throw std::runtime_error( "Invalid animation RLE run" );
		r.Check( nAt, ( nValid + 1 ) * 2 );
		if ( nFrame < nTotal )
			return r.Read<std::int16_t>( nAt + 2 * ( std::min( nFrame, nValid - 1 ) + 1 ) );
		nFrame -= nTotal;
		nAt += ( nValid + 1 ) * 2;
	}
	throw std::runtime_error( "Animation RLE did not reach the requested frame" );
}
//-----------------------------------------------------------------------------
// Purpose: Samples one compressed animation frame from an MDL or a bounded ANI block.
//-----------------------------------------------------------------------------
std::vector<Pose_t> Frame( const CStudioFile &file, const Animation_t &anim, int nFrame )
{
	CBinary mdl( *file.m_pBytes );
	const auto nDesc = anim.m_nOffset;
	const bool bDelta = anim.m_nFlags & 4;
	std::vector<Pose_t> pose;
	for ( const auto &bone : file.m_Bones )
		pose.push_back( bDelta ? Pose_t{} : Pose_t{ bone.m_vecPosition, bone.m_Quaternion } );
	if ( anim.m_nFlags & 32 )
		return pose;
	if ( anim.m_nFlags & 0x40 )
		throw std::runtime_error( "Frame-based animation compression is not supported" );
	if ( mdl.Int( nDesc + 72 ) )
		throw std::runtime_error( "Animation has local hierarchy rules which the viewer cannot evaluate yet" );
	int nBlock = mdl.Int( nDesc + 52 ), nIndex = mdl.Int( nDesc + 56 );
	const int nSectionFrames = mdl.Int( nDesc + 84 );
	if ( nSectionFrames < 0 )
		throw std::runtime_error( "Invalid animation section duration" );
	if ( nSectionFrames > 0 )
	{
		int nSection = nFrame / nSectionFrames;
		if ( anim.m_nFrames > nSectionFrames && nFrame == anim.m_nFrames - 1 )
		{
			nSection = anim.m_nFrames / nSectionFrames + 1;
			nFrame = 0;
		}
		else
			nFrame %= nSectionFrames;
		const auto nAt = mdl.Relative( nDesc, nDesc + 80 ) + static_cast<std::size_t>( nSection ) * 8;
		nBlock = mdl.Int( nAt );
		nIndex = mdl.Int( nAt + 4 );
	}
	if ( !nIndex )
		return pose;
	std::span<const std::byte> data = *file.m_pBytes;
	std::size_t nAt;
	if ( nIndex < 0 || nBlock < 0 )
		throw std::runtime_error( "Invalid animation data offset" );
	if ( nBlock == 0 )
		nAt = nDesc + static_cast<std::size_t>( nIndex );
	else
	{
		if ( !file.m_pAnimationBytes )
			throw std::runtime_error( "External animation file is missing: " + file.m_szAnimationFile );
		if ( nBlock >= static_cast<int>( file.m_AnimationBlocks.size() ) )
			throw std::runtime_error( "Animation block is out of range" );
		const auto [ nStart, nEnd ] = file.m_AnimationBlocks[ nBlock ];
		if ( nStart < 0 || nEnd < nStart )
			throw std::runtime_error( "Invalid ANI block" );
		CBinary( *file.m_pAnimationBytes ).Check( nStart, static_cast<std::size_t>( nEnd - nStart ) );
		data = std::span<const std::byte>( *file.m_pAnimationBytes ).subspan( nStart, nEnd - nStart );
		nAt = nIndex;
	}
	CBinary r( data );
	std::vector<bool> visited( pose.size() );
	for ( std::size_t nTrack = 0; nTrack <= pose.size(); nTrack++ )
	{
		const int nBone = r.Read<std::uint8_t>( nAt ), nFlags = r.Read<std::uint8_t>( nAt + 1 );
		if ( nBone == 255 )
			break;
		if ( nBone >= static_cast<int>( pose.size() ) || visited[ nBone ] )
			throw std::runtime_error( "Animation bone is invalid or repeated" );
		visited[ nBone ] = true;
		const auto &bone = file.m_Bones[ nBone ];
		auto &value = pose[ nBone ];
		const bool bTrackDelta = nFlags & 16;
		value = bTrackDelta ? Pose_t{} : Pose_t{ bone.m_vecPosition, bone.m_Quaternion };
		const auto nPayload = nAt + 4;
		if ( nFlags & ( 2 | 32 ) )
			value.m_Quaternion = PackedRotation( r, nPayload, nFlags & 32 );
		if ( nFlags & 1 )
		{
			const auto nPos = nPayload + ( nFlags & 2 ? 6 : 0 ) + ( nFlags & 32 ? 8 : 0 );
			value.m_vecPosition = { Half( r.Read<std::uint16_t>( nPos ) ), Half( r.Read<std::uint16_t>( nPos + 2 ) ), Half( r.Read<std::uint16_t>( nPos + 4 ) ) };
		}
		if ( nFlags & 8 )
		{
			CVec3 a{ Channel( r, nPayload, 0, nFrame ) * bone.m_vecRotationScale.m_flX, Channel( r, nPayload, 1, nFrame ) * bone.m_vecRotationScale.m_flY, Channel( r, nPayload, 2, nFrame ) * bone.m_vecRotationScale.m_flZ };
			if ( !bTrackDelta )
				a += bone.m_vecAngles;
			value.m_Quaternion = Rotation( a );
		}
		if ( nFlags & 4 )
		{
			const auto nPos = nPayload + ( nFlags & 8 ? 6 : 0 );
			value.m_vecPosition = { Channel( r, nPos, 0, nFrame ) * bone.m_vecPositionScale.m_flX, Channel( r, nPos, 1, nFrame ) * bone.m_vecPositionScale.m_flY, Channel( r, nPos, 2, nFrame ) * bone.m_vecPositionScale.m_flZ };
			if ( !bTrackDelta )
				value.m_vecPosition += bone.m_vecPosition;
		}
		const auto nNext = r.Read<std::int16_t>( nAt + 2 );
		if ( !nNext )
			return pose;
		if ( nNext < 4 )
			throw std::runtime_error( "Invalid animation track link" );
		nAt += nNext;
	}
	return pose;
}
//-----------------------------------------------------------------------------
// Purpose: Interpolates two samples without changing their source data.
//-----------------------------------------------------------------------------
std::vector<Pose_t> Blend( std::vector<Pose_t> a, const std::vector<Pose_t> &b, float flWeight )
{
	for ( std::size_t i = 0; i < a.size(); i++ )
	{
		a[ i ].m_vecPosition = a[ i ].m_vecPosition * ( 1 - flWeight ) + b[ i ].m_vecPosition * flWeight;
		a[ i ].m_Quaternion = BlendRotation( a[ i ].m_Quaternion, b[ i ].m_Quaternion, flWeight );
	}
	return a;
}
} // namespace
//-----------------------------------------------------------------------------
// Purpose: Samples a bilinear sequence blend and maps its skeleton onto the displayed model.
//-----------------------------------------------------------------------------
std::optional<std::vector<CTransform>> SampleSequence( const CStudioFile &source, std::size_t nSequence, float flCycle, std::span<const float> parameters, const CModel &target, std::string *pError )
{
	try
	{
		if ( nSequence >= source.m_Sequences.size() || !std::isfinite( flCycle ) )
			throw std::runtime_error( "Invalid sequence sample" );
		const auto &seq = source.m_Sequences[ nSequence ];
		std::array<int, 2> low{}, high{};
		std::array<float, 2> weight{};
		for ( int axis = 0; axis < 2; axis++ )
		{
			float flValue = seq.m_Start[ axis ];
			const int nParam = seq.m_Parameters[ axis ];
			if ( nParam >= 0 && nParam < static_cast<int>( parameters.size() ) )
				flValue = parameters[ nParam ];
			float flIndex = 0;
			const auto &keys = seq.m_Keys[ axis ];
			if ( keys.size() > 1 )
			{
				int i = 0;
				while ( i + 2 < static_cast<int>( keys.size() ) && flValue > keys[ i + 1 ] )
					i++;
				flIndex = static_cast<float>( i ) + ( keys[ i + 1 ] != keys[ i ] ? std::clamp( ( flValue - keys[ i ] ) / ( keys[ i + 1 ] - keys[ i ] ), 0.0f, 1.0f ) : 0 );
			}
			else if ( seq.m_End[ axis ] != seq.m_Start[ axis ] )
				flIndex = std::clamp( ( flValue - seq.m_Start[ axis ] ) / ( seq.m_End[ axis ] - seq.m_Start[ axis ] ), 0.0f, 1.0f ) * ( seq.m_Size[ axis ] - 1 );
			low[ axis ] = std::clamp( static_cast<int>( flIndex ), 0, seq.m_Size[ axis ] - 1 );
			high[ axis ] = std::min( low[ axis ] + 1, seq.m_Size[ axis ] - 1 );
			weight[ axis ] = flIndex - low[ axis ];
		}
		const auto sample = [ & ]( int x, int y ) {
			const int nAnim = seq.m_Animations.at( y * seq.m_Size[ 0 ] + x );
			if ( nAnim < 0 || nAnim >= static_cast<int>( source.m_Animations.size() ) )
				throw std::runtime_error( "Sequence animation index is invalid" );
			const auto &anim = source.m_Animations[ nAnim ];
			const float flFrame = std::clamp( flCycle, 0.0f, 1.0f ) * std::max( 0, anim.m_nFrames - 1 );
			const int nFirst = static_cast<int>( flFrame ), nNext = std::min( nFirst + 1, std::max( 0, anim.m_nFrames - 1 ) );
			auto a = Frame( source, anim, nFirst );
			return nNext == nFirst ? a : Blend( std::move( a ), Frame( source, anim, nNext ), flFrame - nFirst );
		};
		std::vector<Pose_t> pose;
		if ( seq.m_Animations.empty() )
			for ( const auto &bone : source.m_Bones )
				pose.push_back( seq.m_nFlags & 4 ? Pose_t{} : Pose_t{ bone.m_vecPosition, bone.m_Quaternion } );
		else
			pose = sample( low[ 0 ], low[ 1 ] );
		if ( high[ 0 ] != low[ 0 ] )
			pose = Blend( std::move( pose ), sample( high[ 0 ], low[ 1 ] ), weight[ 0 ] );
		if ( high[ 1 ] != low[ 1 ] )
		{
			auto upper = sample( low[ 0 ], high[ 1 ] );
			if ( high[ 0 ] != low[ 0 ] )
				upper = Blend( std::move( upper ), sample( high[ 0 ], high[ 1 ] ), weight[ 0 ] );
			pose = Blend( std::move( pose ), upper, weight[ 1 ] );
		}
		std::vector<CTransform> local;
		for ( std::size_t i = 0; i < pose.size(); i++ )
		{
			const auto &bone = source.m_Bones[ i ];
			const float flWeight = i < seq.m_BoneWeights.size() ? std::clamp( seq.m_BoneWeights[ i ], 0.0f, 1.0f ) : 1;
			if ( seq.m_nFlags & 4 )
			{
				Pose_t delta = pose[ i ];
				delta.m_vecPosition = bone.m_vecPosition + delta.m_vecPosition * flWeight;
				const auto a = Transform( { {}, bone.m_Quaternion } ), b = Transform( { {}, BlendRotation( { 0, 0, 0, 1 }, pose[ i ].m_Quaternion, flWeight ) } );
				local.push_back( CTransform::Translation( delta.m_vecPosition ) * a * b );
			}
			else
				local.push_back( Transform( Blend( { Pose_t{ bone.m_vecPosition, bone.m_Quaternion } }, { pose[ i ] }, flWeight )[ 0 ] ) );
		}
		std::unordered_map<std::string, std::size_t> indices;
		for ( std::size_t i = 0; i < source.m_Bones.size(); i++ )
			indices.emplace( source.m_Bones[ i ].m_szName, i );
		std::vector<CTransform> result( target.m_Bones.size() );
		std::vector<int> state( result.size() );
		const auto build = [ & ]( auto &&self, std::size_t i ) -> void {
			if ( state[ i ] == 2 )
				return;
			if ( state[ i ] == 1 )
				throw std::runtime_error( "Cyclic bone hierarchy" );
			state[ i ] = 1;
			const auto &bone = target.m_Bones[ i ];
			const auto found = indices.find( bone.m_szName );
			CTransform transform;
			if ( found != indices.end() )
				transform = local[ found->second ];
			else if ( bone.m_nParent >= 0 && bone.m_nParent < static_cast<int>( target.m_Bones.size() ) )
				transform = target.m_Bones[ bone.m_nParent ].m_ToModel.Inverse().value_or( CTransform{} ) * bone.m_ToModel;
			else
				transform = bone.m_ToModel;
			if ( bone.m_nParent >= 0 )
			{
				if ( bone.m_nParent >= static_cast<int>( result.size() ) )
					throw std::runtime_error( "Invalid bone parent" );
				self( self, bone.m_nParent );
				transform = result[ bone.m_nParent ] * transform;
			}
			result[ i ] = transform;
			state[ i ] = 2;
		};
		for ( std::size_t i = 0; i < result.size(); i++ )
			build( build, i );
		return result;
	}
	catch ( const std::exception &error )
	{
		if ( pError )
			*pError = error.what();
		return std::nullopt;
	}
}
//-----------------------------------------------------------------------------
// Purpose: Applies up to three weighted bone transforms to positions and normals.
//-----------------------------------------------------------------------------
CModel Deform( const CModel &reference, std::span<const CTransform> pose )
{
	CModel out = reference;
	std::vector<CTransform> transforms;
	for ( std::size_t i = 0; i < reference.m_Bones.size(); i++ )
	{
		const auto transform = i < pose.size() ? pose[ i ] : reference.m_Bones[ i ].m_ToModel;
		out.m_Bones[ i ].m_ToModel = transform;
		transforms.push_back( transform * reference.m_Bones[ i ].m_ToModel.Inverse().value_or( CTransform{} ) );
	}
	for ( auto &vertex : out.m_Vertices )
	{
		CVec3 vecPosition{}, vecNormal{};
		float flTotal = 0;
		for ( int i = 0; i < vertex.m_nBones && i < 3; i++ )
		{
			const int nBone = vertex.m_Bones[ i ];
			const float flWeight = vertex.m_Weights[ i ];
			if ( nBone < 0 || nBone >= static_cast<int>( transforms.size() ) || !std::isfinite( flWeight ) || flWeight <= 0 )
				continue;
			vecPosition += transforms[ nBone ].Apply( vertex.m_vecPosition ) * flWeight;
			vecNormal += transforms[ nBone ].Rotate( vertex.m_vecNormal ) * flWeight;
			flTotal += flWeight;
		}
		if ( flTotal > 0 )
		{
			vertex.m_vecPosition = vecPosition * ( 1.0f / flTotal );
			vertex.m_vecNormal = Normalized( vecNormal );
		}
	}
	return out;
}
} // namespace xv::model
