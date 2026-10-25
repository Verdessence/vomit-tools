//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Studio layout facts from public SDK headers, decoded into checked independent views.
//===========================================================================//
#include "Mdl.h"
#include "Binary.h"
#include "vfs/Vfs.h"
#include <set>

namespace xv::model
{
//-----------------------------------------------------------------------------
// Purpose: Reads metadata even for animation-only models, without requiring vertex companions.
//-----------------------------------------------------------------------------
std::optional<CStudioFile> ReadStudio( std::string_view szPath, const Reader_t &pfnRead, std::string *pError )
{
	try
	{
		auto bytes = pfnRead( szPath );
		if ( !bytes )
			throw std::runtime_error( std::string( szPath ) + " is missing" );
		CBinary r( *bytes );
		r.Check( 0, 408 );
		if ( r.Int( 0 ) != 0x54534449 )
			throw std::runtime_error( "Not a Source studio model" );
		if ( r.Int( 156 ) < 0 || r.Int( 156 ) > 1024 )
			throw std::runtime_error( "Model bone count exceeds the viewer limit" );
		CStudioFile file;
		file.m_szPath = szPath;
		file.m_nVersion = r.Int( 4 );
		if ( file.m_nVersion < 44 || file.m_nVersion > 49 )
			throw std::runtime_error( "Supported model versions are 44–49" );
		if ( r.Int( 76 ) < 408 || static_cast<std::size_t>( r.Int( 76 ) ) != bytes->size() )
			throw std::runtime_error( "The MDL's declared length does not match its file" );
		file.m_nChecksum = r.Int( 8 );
		file.m_szName = r.FixedText( 12, 64 );
		file.m_vecEye = r.Vector( 80 );
		file.m_vecIllumination = r.Vector( 92 );
		file.m_vecHullMin = r.Vector( 104 );
		file.m_vecHullMax = r.Vector( 116 );
		file.m_vecViewMin = r.Vector( 128 );
		file.m_vecViewMax = r.Vector( 140 );
		file.m_nFlags = r.Int( 152 );
		file.m_flMass = r.Read<float>( 328 );
		file.m_nContents = r.Int( 332 );
		file.m_szSurface = r.Name( 0, 308 );
		const int nText = r.Int( 316 );
		if ( nText )
		{
			const auto nAt = r.Relative( 0, 312 );
			r.Array( nAt, nText, 1 );
			file.m_szKeyValues = r.FixedText( nAt, nText );
		}
		const auto each = [ & ]( std::size_t nHeader, std::size_t nStride, auto pfnReadRecord ) {
			const int nCount = r.Int( nHeader );
			if ( !nCount )
				return;
			const auto nFirst = r.Relative( 0, nHeader + 4 );
			r.Array( nFirst, nCount, nStride );
			for ( int i = 0; i < nCount; i++ )
				pfnReadRecord( nFirst + i * nStride );
		};
		each( 156, 216, [ & ]( std::size_t n ) {
			StudioBone_t bone;
			bone.m_szName = r.Name( n, n );
			bone.m_nParent = r.Int( n + 4 );
			bone.m_vecPosition = r.Vector( n + 32 );
			bone.m_vecAngles = r.Vector( n + 60 );
			for ( int i = 0; i < 4; i++ )
				bone.m_Quaternion[ i ] = r.Read<float>( n + 44 + i * 4 );
			bone.m_vecPositionScale = r.Vector( n + 72 );
			bone.m_vecRotationScale = r.Vector( n + 84 );
			for ( int i = 0; i < 12; i++ )
				bone.m_PoseToBone.m_Values[ i ] = r.Read<float>( n + 96 + i * 4 );
			bone.m_nProcedure = r.Int( n + 164 );
			file.m_Bones.push_back( bone );
		} );
		each( 164, 56, [ & ]( std::size_t n ) { file.m_Controllers.push_back( { r.Int( n ), r.Int( n + 4 ), r.Int( n + 20 ), r.Read<float>( n + 8 ), r.Read<float>( n + 12 ) } ); } );
		each( 172, 12, [ & ]( std::size_t n ) {
			HitboxSet_t set;
			set.m_szName = r.Name( n, n );
			const auto nBoxes = r.Relative( n, n + 8 );
			const int nCount = r.Array( nBoxes, r.Int( n + 4 ), 68 );
			for ( int i = 0; i < nCount; i++ )
			{
				const auto b = nBoxes + i * 68;
				set.m_Boxes.push_back( { r.Int( b ), r.Int( b + 4 ), r.Vector( b + 8 ), r.Vector( b + 20 ), r.Name( r.HitboxNameBase( file.m_nVersion, b ), b + 32 ) } );
			}
			file.m_HitboxSets.push_back( std::move( set ) );
		} );
		each( 180, 100, [ & ]( std::size_t n ) {
			Animation_t anim;
			anim.m_nOffset = n;
			anim.m_szName = r.Name( n, n + 4 );
			anim.m_flFps = r.Read<float>( n + 8 );
			anim.m_nFlags = r.Int( n + 12 );
			anim.m_nFrames = r.Int( n + 16 );
			if ( anim.m_nFrames < 0 || anim.m_nFrames > ( 1 << 20 ) || anim.m_flFps < 0 )
				throw std::runtime_error( "Invalid animation duration" );
			file.m_Animations.push_back( std::move( anim ) );
		} );
		each( 188, 212, [ & ]( std::size_t n ) {
			Sequence_t seq;
			seq.m_szName = r.Name( n, n + 4 );
			seq.m_szActivity = r.Name( n, n + 8 );
			seq.m_nFlags = r.Int( n + 12 );
			seq.m_nWeight = r.Int( n + 20 );
			seq.m_flFadeIn = r.Read<float>( n + 104 );
			seq.m_flFadeOut = r.Read<float>( n + 108 );
			for ( int i = 0; i < 2; i++ )
			{
				seq.m_Size[ i ] = r.Int( n + 68 + i * 4 );
				seq.m_Parameters[ i ] = r.Int( n + 76 + i * 4 );
				seq.m_Start[ i ] = r.Read<float>( n + 84 + i * 4 );
				seq.m_End[ i ] = r.Read<float>( n + 92 + i * 4 );
				if ( seq.m_Size[ i ] < 0 || seq.m_Size[ i ] > 1024 )
					throw std::runtime_error( "Invalid animation blend grid" );
			}
			const int nBlends = seq.m_Size[ 0 ] * seq.m_Size[ 1 ];
			for ( int &nSize : seq.m_Size )
				nSize = std::max( 1, nSize );
			const auto nAnims = r.Relative( n, n + 60 );
			r.Array( nAnims, nBlends, 2 );
			for ( int i = 0; i < nBlends; i++ )
				seq.m_Animations.push_back( r.Read<std::int16_t>( nAnims + i * 2 ) );
			const auto nEvents = r.Relative( n, n + 28 );
			const int nCount = r.Array( nEvents, r.Int( n + 24 ), 80 );
			for ( int i = 0; i < nCount; i++ )
			{
				const auto e = nEvents + i * 80;
				const float flCycle = std::bit_cast<float>( r.Read<std::uint32_t>( e ) );
				const bool bValidCycle = std::isfinite( flCycle ) && flCycle >= 0 && flCycle <= 1;
				if ( !bValidCycle )
					file.m_Problems.push_back( "Sequence " + seq.m_szName + " has an event with no valid cycle" );
				seq.m_Events.push_back( { bValidCycle ? flCycle : 0, r.Int( e + 4 ), bValidCycle, r.Name( e, e + 76 ), r.FixedText( e + 12, 64 ), r.Int( e + 8 ) } );
			}
			if ( r.Int( n + 156 ) )
			{
				const auto nWeights = r.Relative( n, n + 156 );
				r.Array( nWeights, static_cast<int>( file.m_Bones.size() ), 4 );
				for ( std::size_t i = 0; i < file.m_Bones.size(); i++ )
					seq.m_BoneWeights.push_back( r.Read<float>( nWeights + i * 4 ) );
			}
			if ( r.Int( n + 160 ) )
			{
				const auto nKeys = r.Relative( n, n + 160 );
				r.Array( nKeys, seq.m_Size[ 0 ] + seq.m_Size[ 1 ], 4 );
				for ( int axis = 0; axis < 2; axis++ )
					for ( int i = 0; i < seq.m_Size[ axis ]; i++ )
						seq.m_Keys[ axis ].push_back( r.Read<float>( nKeys + ( i + ( axis ? seq.m_Size[ 0 ] : 0 ) ) * 4 ) );
			}
			seq.m_bHasLayers = r.Int( n + 148 ) > 0;
			file.m_Sequences.push_back( std::move( seq ) );
		} );
		each( 212, 4, [ & ]( std::size_t n ) { file.m_Folders.push_back( r.Name( 0, n ) ); } );
		const int nRefs = r.Int( 220 ), nSkins = r.Int( 224 );
		if ( nRefs < 0 || nRefs > 4096 || nSkins < 0 || nSkins > 4096 )
			throw std::runtime_error( "Invalid skin table" );
		const auto nSkinAt = r.Relative( 0, 228 );
		r.Array( nSkinAt, nRefs * nSkins, 2 );
		for ( int i = 0; i < nSkins; i++ )
		{
			std::vector<int> skin;
			for ( int j = 0; j < nRefs; j++ )
				skin.push_back( r.Read<std::int16_t>( nSkinAt + ( i * nRefs + j ) * 2 ) );
			file.m_Skins.push_back( std::move( skin ) );
		}
		each( 232, 16, [ & ]( std::size_t n ) {
			Bodygroup_t group;
			group.m_szName = r.Name( n, n );
			const auto nModels = r.Relative( n, n + 12 );
			const int nCount = r.Array( nModels, r.Int( n + 4 ), 148 );
			for ( int i = 0; i < nCount; i++ )
			{
				const auto m = nModels + i * 148;
				group.m_Models.push_back( r.FixedText( m, 64 ) );
				const int nEyeCount = r.Int( m + 100 );
				if ( !nEyeCount )
					continue;
				const auto nEyes = r.Relative( m, m + 104 );
				r.Array( nEyes, nEyeCount, 172 );
				for ( int j = 0; j < nEyeCount; j++ )
				{
					const auto e = nEyes + j * 172;
					file.m_Eyeballs.push_back( { r.Name( e, e ), r.Int( e + 4 ), r.Vector( e + 8 ), r.Read<float>( e + 24 ) } );
				}
			}
			file.m_Bodygroups.push_back( std::move( group ) );
		} );
		each( 260, 4, [ & ]( std::size_t n ) { file.m_Flexes.push_back( r.Name( n, n ) ); } );
		each( 300, 20, [ & ]( std::size_t n ) { file.m_Parameters.push_back( { r.Name( n, n ), r.Read<float>( n + 8 ), r.Read<float>( n + 12 ), r.Read<float>( n + 16 ) } ); } );
		each( 336, 8, [ & ]( std::size_t n ) { file.m_Includes.push_back( vfs::NormalizePath( r.Name( n, n + 4 ) ) ); } );
		file.m_szAnimationFile = r.Name( 0, 348 );
		each( 352, 8, [ & ]( std::size_t n ) { file.m_AnimationBlocks.emplace_back( r.Int( n ), r.Int( n + 4 ) ); } );
		if ( !file.m_szAnimationFile.empty() )
		{
			auto ani = pfnRead( file.m_szAnimationFile );
			if ( !ani && !file.m_szAnimationFile.starts_with( "models/" ) )
				ani = pfnRead( "models/" + file.m_szAnimationFile );
			if ( ani )
				file.m_pAnimationBytes = std::make_shared<const std::vector<std::byte>>( std::move( *ani ) );
		}
		file.m_pBytes = std::make_shared<const std::vector<std::byte>>( std::move( *bytes ) );
		return file;
	}
	catch ( const std::exception &error )
	{
		if ( pError )
			*pError = error.what();
		return std::nullopt;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Loads the include graph once per path, with limits on depth and total size.
//-----------------------------------------------------------------------------
std::vector<CStudioFile> LoadIncludes( const CStudioFile &file, const Reader_t &pfnRead, std::vector<std::string> &problems )
{
	std::vector<CStudioFile> files;
	std::vector<std::string> pending = file.m_Includes;
	std::set<std::string> visited{ vfs::NormalizePath( file.m_szPath ) };
	std::size_t nTotal = file.m_pBytes->size();
	for ( std::size_t i = 0; i < pending.size(); i++ )
	{
		std::string szPath = vfs::NormalizePath( pending[ i ] );
		if ( !szPath.starts_with( "models/" ) )
			szPath = "models/" + szPath;
		if ( !visited.insert( szPath ).second )
			continue;
		if ( visited.size() > 256 || nTotal > 512 * 1024 * 1024 )
		{
			problems.push_back( "Include graph exceeds the viewer's resource limit" );
			break;
		}
		std::string szError;
		if ( auto included = ReadStudio( szPath, pfnRead, &szError ) )
		{
			nTotal += included->m_pBytes->size();
			pending.insert( pending.end(), included->m_Includes.begin(), included->m_Includes.end() );
			files.push_back( std::move( *included ) );
		}
		else
			problems.push_back( szPath + ": " + szError );
	}
	return files;
}
} // namespace xv::model
