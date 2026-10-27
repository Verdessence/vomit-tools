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
	if ( IsSolidObject( nObject ) )
	{
		return m_Map.m_Solids[ nObject ].m_nEntity;
	}
	const std::size_t nEntity = nObject - m_Solids.size();
	return nEntity < m_Entities.size() ? static_cast<int>( nEntity ) : -1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const EditorInfo_t &CScene::GetObjectEditor( std::uint32_t nObject ) const
{
	if ( IsSolidObject( nObject ) )
	{
		return m_Map.m_Solids[ nObject ].m_Editor;
	}
	return m_Map.m_Entities[ nObject - m_Solids.size() ].m_Editor;
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
	if ( IsSolidObject( nObject ) )
	{
		return m_Map.m_Solids[ nObject ].m_nId;
	}
	const std::size_t nEntity = nObject - m_Solids.size();
	return nEntity < m_Map.m_Entities.size() ? m_Map.m_Entities[ nEntity ].m_nId : 0;
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
	if ( !progress( 20, "solids" ) )
	{
		return nullptr;
	}
	pScene->m_Solids = BuildSolids( pScene->m_Map );
	if ( !progress( 50, "entities" ) )
	{
		return nullptr;
	}
	pScene->m_Entities.reserve( pScene->m_Map.m_Entities.size() );
	for ( const Entity_t &entity : pScene->m_Map.m_Entities )
	{
		pScene->m_Entities.push_back( DescribeEntity( entity, pScene->m_pFgd.get() ) );
	}
	if ( !progress( 60, "mesh" ) )
	{
		return nullptr;
	}
	pScene->m_Mesh = BuildMesh( pScene->m_Map, pScene->m_Solids );
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
