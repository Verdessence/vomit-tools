//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Scene.h.
//
//===========================================================================//
#include "Scene.h"

namespace xv::map
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CScene::GetObjectEntity( std::uint32_t nObject ) const
{
	if ( nObject >= m_Objects.size() )
	{
		return -1;
	}
	const ObjectRef_t object = m_Objects[ nObject ];
	return object.m_bSolid ? m_Map.m_Solids[ object.m_nIndex ].m_nEntity : static_cast<int>( object.m_nIndex );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CScene::IsDeleted( std::uint32_t nObject ) const
{
	if ( nObject >= m_Objects.size() )
	{
		return true;
	}
	const ObjectRef_t object = m_Objects[ nObject ];
	return object.m_bSolid ? m_Map.m_Solids[ object.m_nIndex ].m_bDeleted : m_Map.m_Entities[ object.m_nIndex ].m_bDeleted;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CScene::NumberObjects()
{
	m_Objects.clear();
	m_SolidObjects.resize( m_Map.m_Solids.size() );
	m_EntityObjects.resize( m_Map.m_Entities.size() );
	for ( std::uint32_t s = 0; s < m_Map.m_Solids.size(); s++ )
	{
		m_SolidObjects[ s ] = static_cast<std::uint32_t>( m_Objects.size() );
		m_Objects.push_back( { true, s } );
	}
	for ( std::uint32_t e = 0; e < m_Map.m_Entities.size(); e++ )
	{
		m_EntityObjects[ e ] = static_cast<std::uint32_t>( m_Objects.size() );
		m_Objects.push_back( { false, e } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const EditorInfo_t &CScene::GetObjectEditor( std::uint32_t nObject ) const
{
	const ObjectRef_t object = m_Objects[ nObject ];
	return object.m_bSolid ? m_Map.m_Solids[ object.m_nIndex ].m_Editor : m_Map.m_Entities[ object.m_nIndex ].m_Editor;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Box_t CScene::GetEntityBox( std::size_t nEntity ) const
{
	const EntityVisual_t &visual = m_Entities[ nEntity ];
	Box_t box;
	box.Add( Add( visual.m_Origin, visual.m_Box.m_Mins ) );
	box.Add( Add( visual.m_Origin, visual.m_Box.m_Maxs ) );
	return box;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CScene::GetObjectId( std::uint32_t nObject ) const
{
	if ( nObject >= m_Objects.size() )
	{
		return 0;
	}
	const ObjectRef_t object = m_Objects[ nObject ];
	return object.m_bSolid ? m_Map.m_Solids[ object.m_nIndex ].m_nId : m_Map.m_Entities[ object.m_nIndex ].m_nId;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::unique_ptr<CScene> BuildScene( CMap map, std::shared_ptr<const CFgd> pFgd, const SceneOptions_t &options )
{
	auto progress = [ & ]( int nPercent, std::string_view szStage ) {
		if ( options.m_pfnProgress )
		{
			options.m_pfnProgress( nPercent, szStage );
		}
		return !options.m_pCancel || !options.m_pCancel->load();
	};
	auto pScene = std::make_unique<CScene>();
	pScene->m_Map = std::move( map );
	pScene->m_pFgd = std::move( pFgd );
	if ( const auto size = pScene->m_pFgd ? pScene->m_pFgd->GetMapSize() : std::nullopt )
	{
		pScene->m_flWorldLimit = std::max( std::abs( size->first ), std::abs( size->second ) );
	}
	if ( !progress( 20, "solids" ) )
	{
		return nullptr;
	}
	pScene->m_Solids = CSharedVector<SolidGeometry_t>{ BuildSolids( pScene->m_Map ) };
	if ( !progress( 50, "entities" ) )
	{
		return nullptr;
	}
	pScene->m_Entities.reserve( pScene->m_Map.m_Entities.size() );
	for ( const Entity_t &entity : pScene->m_Map.m_Entities )
	{
		pScene->m_Entities.push_back( DescribeEntity( entity, pScene->m_pFgd.get() ) );
	}
	pScene->NumberObjects();
	if ( !progress( 60, "mesh" ) )
	{
		return nullptr;
	}
	{
		std::vector<MeshSolid_t> solids( pScene->m_Solids.size() );
		for ( std::size_t i = 0; i < solids.size(); i++ )
		{
			solids[ i ] = { static_cast<std::uint32_t>( i ), static_cast<std::uint32_t>( i ), &std::as_const( pScene->m_Solids )[ i ] };
		}
		CMapMesh mesh = BuildMesh( std::as_const( pScene->m_Map ), std::span<const MeshSolid_t>{ solids } );
		mesh.m_SolidIndices.clear();
		pScene->m_pMesh = std::make_shared<const CMapMesh>( std::move( mesh ) );
	}
	pScene->m_Materials = pScene->m_Map.m_Materials;
	if ( !progress( 80, "instances" ) )
	{
		return nullptr;
	}
	LoadInstances( *pScene, options );
	pScene->m_Skybox3D = FindSkybox3D( *pScene );
	for ( const SolidGeometry_t &solid : pScene->m_Solids )
	{
		pScene->m_Bounds.Add( solid.m_Bounds );
	}
	for ( std::size_t i = 0; i < pScene->m_Entities.size(); i++ )
	{
		if ( pScene->m_Entities[ i ].m_bPoint )
		{
			pScene->m_Bounds.Add( pScene->GetEntityBox( i ) );
		}
	}
	for ( const MeshChunk_t &chunk : pScene->GetInstanceMesh().m_Chunks )
	{
		pScene->m_Bounds.Add( chunk.m_Bounds );
	}
	pScene->m_AutoVisgroups = BuildAutoVisgroups( *pScene );
	progress( 100, "done" );
	return pScene;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<const CFgd> LoadFgds( const std::vector<std::filesystem::path> &files )
{
	if ( files.empty() )
	{
		return nullptr;
	}
	auto pFgd = std::make_shared<CFgd>();
	for ( const std::filesystem::path &path : files )
	{
		pFgd->AddFile( path );
	}
	pFgd->Finish();
	return pFgd;
}

} // namespace xv::map
