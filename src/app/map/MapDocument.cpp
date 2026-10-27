//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapDocument.h.
//
//===========================================================================//
#include "MapDocument.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "map/GameConfig.h"
#include "map/Vmf.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Whether sorted contains n.
//-----------------------------------------------------------------------------
bool Contains( const std::vector<std::uint32_t> &sorted, std::uint32_t n )
{
	return std::ranges::binary_search( sorted, n );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SortUnique( std::vector<std::uint32_t> &objects )
{
	std::ranges::sort( objects );
	objects.erase( std::unique( objects.begin(), objects.end() ), objects.end() );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapDocument::CMapDocument( QObject *pParent )
	: QObject( pParent )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapDocument::~CMapDocument()
{
	Stop();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::Stop()
{
	m_bStop = true;
	if ( m_Worker.joinable() )
	{
		m_Worker.join();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::Load( Source_t source )
{
	Stop();
	m_bStop = false;
	m_Game = source.m_Game;
	const int nGeneration = ++m_nGeneration;
	m_Worker = std::thread( [ this, source = std::move( source ), nGeneration ]() mutable { Run( std::move( source ), nGeneration ); } );
}

//-----------------------------------------------------------------------------
// Purpose: The thread: reads the FGDs and the map, then builds everything the views draw.
//          Results go back to the window as queued calls tagged with the load's number.
//-----------------------------------------------------------------------------
void CMapDocument::Run( Source_t source, int nGeneration )
{
	const auto post = [ this, nGeneration ]( auto pfnCall ) {
		QMetaObject::invokeMethod(
			this, [ this, nGeneration, pfnCall = std::move( pfnCall ) ]() mutable {
				if ( nGeneration == m_nGeneration )
				{
					pfnCall();
				}
			},
			Qt::QueuedConnection );
	};
	const auto progress = [ & ]( int nPercent, std::string_view szStage ) {
		post( [ this, nPercent, szStage = QString::fromUtf8( szStage.data(), static_cast<qsizetype>( szStage.size() ) ) ] { emit LoadProgress( nPercent, szStage ); } );
	};

	progress( 0, "reading" );
	std::string szError;
	std::optional<xv::map::CMap> map = xv::map::ReadVmf( std::move( source.m_Text ), {}, &szError );
	if ( !map )
	{
		post( [ this, szError = QString::fromStdString( szError ) ] { emit LoadFailed( szError ); } );
		return;
	}
	if ( m_bStop )
	{
		return;
	}

	progress( 10, "game data" );
	std::vector<std::filesystem::path> fgds = source.m_Fgds;
	QString szFgdSource = tr( "Preferences" );
	if ( fgds.empty() && source.m_Game )
	{
		xv::map::GameFgds_t found = xv::map::FindGameFgds( source.m_Game->m_GameDir );
		fgds = std::move( found.m_Files );
		szFgdSource = QString::fromStdString( found.m_szSource );
	}
	if ( fgds.empty() )
	{
		szFgdSource.clear();
	}
	std::shared_ptr<const xv::map::CFgd> pFgd = xv::map::LoadFgds( fgds );

	xv::map::SceneOptions_t options;
	options.m_pCancel = &m_bStop;
	options.m_pfnProgress = progress;
	std::shared_ptr<xv::map::CScene> pScene = xv::map::BuildScene( std::move( *map ), std::move( pFgd ), options );
	if ( !pScene )
	{
		return;
	}
	post( [ this, pScene = std::move( pScene ), szFgdSource ]() mutable { Finish( std::move( pScene ), szFgdSource ); } );
}

//-----------------------------------------------------------------------------
// Purpose: The map is built: what starts hidden follows the flags Hammer saved.
//-----------------------------------------------------------------------------
void CMapDocument::Finish( std::shared_ptr<xv::map::CScene> pScene, QString szFgdSource )
{
	m_szFgdSource = std::move( szFgdSource );
	const std::uint32_t nObjects = pScene->GetObjectCount();
	m_UserShown.assign( nObjects, 1 );
	m_AutoShown.assign( nObjects, 1 );
	m_QuickHidden.assign( nObjects, 0 );
	for ( std::uint32_t i = 0; i < nObjects; i++ )
	{
		const xv::map::EditorInfo_t &editor = pScene->GetObjectEditor( i );
		m_UserShown[ i ] = editor.m_bVisgroupShown ? 1 : 0;
		m_AutoShown[ i ] = editor.m_bAutoShown ? 1 : 0;
	}
	m_Selection.clear();
	m_pScene = std::move( pScene );
	m_nStateGeneration++;
	emit Loaded();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::Changed( bool bVisibility )
{
	m_nStateGeneration++;
	if ( bVisibility )
	{
		emit VisibilityChanged();
	}
	else
	{
		emit SelectionChanged();
	}
}

//-----------------------------------------------------------------------------
// Purpose: An entity's solids follow the entity as well as their own flags.
//-----------------------------------------------------------------------------
bool CMapDocument::IsShown( std::uint32_t nObject ) const
{
	if ( !m_pScene || nObject >= m_UserShown.size() )
	{
		return false;
	}
	auto own = [ this ]( std::uint32_t n ) { return m_UserShown[ n ] && m_AutoShown[ n ] && !m_QuickHidden[ n ]; };
	if ( !own( nObject ) )
	{
		return false;
	}
	if ( m_pScene->IsSolidObject( nObject ) )
	{
		const int nEntity = m_pScene->m_Map.m_Solids[ nObject ].m_nEntity;
		return nEntity < 0 || own( m_pScene->EntityObject( static_cast<std::size_t>( nEntity ) ) );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> CMapDocument::GetVisgroupMembers( int nVisgroupId ) const
{
	std::vector<std::uint32_t> members;
	if ( !m_pScene )
	{
		return members;
	}
	// The visgroup and every visgroup nested in it.
	std::set<int> ids{ nVisgroupId };
	const auto &visgroups = m_pScene->m_Map.m_Visgroups;
	for ( bool bGrew = true; bGrew; )
	{
		bGrew = false;
		for ( const xv::map::Visgroup_t &visgroup : visgroups )
		{
			if ( visgroup.m_nParent >= 0 && ids.contains( visgroups[ static_cast<std::size_t>( visgroup.m_nParent ) ].m_nId ) && ids.insert( visgroup.m_nId ).second )
			{
				bGrew = true;
			}
		}
	}
	for ( std::uint32_t i = 0; i < m_pScene->GetObjectCount(); i++ )
	{
		const std::vector<int> &own = m_pScene->GetObjectEditor( i ).m_Visgroups;
		if ( std::ranges::any_of( own, [ & ]( int nId ) { return ids.contains( nId ); } ) )
		{
			members.push_back( i );
		}
	}
	return members;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapDocument::GetVisgroupState( int nVisgroupId ) const
{
	const std::vector<std::uint32_t> members = GetVisgroupMembers( nVisgroupId );
	const auto nShown = std::ranges::count_if( members, [ this ]( std::uint32_t n ) { return m_UserShown[ n ] != 0; } );
	if ( nShown == static_cast<std::ptrdiff_t>( members.size() ) )
	{
		return Qt::Checked;
	}
	return nShown == 0 ? Qt::Unchecked : Qt::PartiallyChecked;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::SetVisgroupShown( int nVisgroupId, bool bShown )
{
	for ( const std::uint32_t n : GetVisgroupMembers( nVisgroupId ) )
	{
		m_UserShown[ n ] = bShown ? 1 : 0;
	}
	Changed( true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapDocument::GetAutoState( const std::vector<std::uint32_t> &objects ) const
{
	const auto nShown = std::ranges::count_if( objects, [ this ]( std::uint32_t n ) { return n < m_AutoShown.size() && m_AutoShown[ n ] != 0; } );
	if ( nShown == static_cast<std::ptrdiff_t>( objects.size() ) )
	{
		return Qt::Checked;
	}
	return nShown == 0 ? Qt::Unchecked : Qt::PartiallyChecked;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::SetAutoShown( const std::vector<std::uint32_t> &objects, bool bShown )
{
	for ( const std::uint32_t n : objects )
	{
		if ( n < m_AutoShown.size() )
		{
			m_AutoShown[ n ] = bShown ? 1 : 0;
		}
	}
	Changed( true );
}

//-----------------------------------------------------------------------------
// Purpose: Hides objects until unhidden; they also leave the selection, as in Hammer.
//-----------------------------------------------------------------------------
void CMapDocument::QuickHide( const std::vector<std::uint32_t> &objects )
{
	for ( const std::uint32_t n : objects )
	{
		if ( n < m_QuickHidden.size() )
		{
			m_QuickHidden[ n ] = 1;
		}
	}
	std::erase_if( m_Selection, [ this ]( std::uint32_t n ) { return m_QuickHidden[ n ] != 0; } );
	Changed( true );
	emit SelectionChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::QuickHideAllBut( const std::vector<std::uint32_t> &objects )
{
	std::vector<std::uint32_t> kept = objects;
	SortUnique( kept );
	// Keeping a solid of an entity keeps the entity too, or the solid would vanish with it.
	if ( m_pScene )
	{
		for ( const std::uint32_t n : objects )
		{
			if ( const int nEntity = m_pScene->GetObjectEntity( n ); nEntity >= 0 )
			{
				kept.push_back( m_pScene->EntityObject( static_cast<std::size_t>( nEntity ) ) );
			}
		}
		SortUnique( kept );
	}
	for ( std::uint32_t n = 0; n < m_QuickHidden.size(); n++ )
	{
		if ( !Contains( kept, n ) )
		{
			m_QuickHidden[ n ] = 1;
		}
	}
	Changed( true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::QuickUnhideAll()
{
	std::ranges::fill( m_QuickHidden, 0 );
	Changed( true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::ShowAll()
{
	std::ranges::fill( m_UserShown, 1 );
	std::ranges::fill( m_AutoShown, 1 );
	std::ranges::fill( m_QuickHidden, 0 );
	Changed( true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapDocument::IsSelected( std::uint32_t nObject ) const
{
	return Contains( m_Selection, nObject );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::SetSelection( std::vector<std::uint32_t> objects )
{
	SortUnique( objects );
	if ( objects == m_Selection )
	{
		return;
	}
	m_Selection = std::move( objects );
	Changed( false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::AddToSelection( const std::vector<std::uint32_t> &objects )
{
	std::vector<std::uint32_t> selection = m_Selection;
	selection.insert( selection.end(), objects.begin(), objects.end() );
	SetSelection( std::move( selection ) );
}

//-----------------------------------------------------------------------------
// Purpose: Ctrl+click: what is all selected already leaves the selection, otherwise it joins.
//-----------------------------------------------------------------------------
void CMapDocument::ToggleSelection( const std::vector<std::uint32_t> &objects )
{
	const bool bAllSelected = !objects.empty() && std::ranges::all_of( objects, [ this ]( std::uint32_t n ) { return IsSelected( n ); } );
	if ( bAllSelected )
	{
		std::vector<std::uint32_t> selection = m_Selection;
		std::erase_if( selection, [ & ]( std::uint32_t n ) { return std::ranges::find( objects, n ) != objects.end(); } );
		SetSelection( std::move( selection ) );
	}
	else
	{
		AddToSelection( objects );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::ClearSelection()
{
	SetSelection( {} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::SetSelectMode( MapSelectMode_t nMode )
{
	if ( nMode != m_nSelectMode )
	{
		m_nSelectMode = nMode;
		emit SelectModeChanged();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint32_t> CMapDocument::Expand( std::uint32_t nObject ) const
{
	std::vector<std::uint32_t> objects{ nObject };
	if ( !m_pScene || m_nSelectMode == MapSelectMode_t::MAP_SELECT_MODE_SOLIDS || nObject >= m_pScene->GetObjectCount() )
	{
		return objects;
	}
	// An entity comes with its solids, and a solid with its entity.
	auto withEntity = [ this ]( std::uint32_t n, std::vector<std::uint32_t> &into ) {
		const int nEntity = m_pScene->GetObjectEntity( n );
		if ( nEntity < 0 )
		{
			return;
		}
		into.push_back( m_pScene->EntityObject( static_cast<std::size_t>( nEntity ) ) );
		for ( const std::uint32_t nSolid : m_pScene->m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ].m_Solids )
		{
			into.push_back( nSolid );
		}
	};
	withEntity( nObject, objects );
	if ( m_nSelectMode == MapSelectMode_t::MAP_SELECT_MODE_GROUPS )
	{
		// The outermost group: groups nest through their own editor's group id.
		std::unordered_map<int, int> parents;
		for ( const xv::map::Group_t &group : m_pScene->m_Map.m_Groups )
		{
			parents[ group.m_nId ] = group.m_Editor.m_nGroup;
		}
		auto topGroup = [ &parents ]( int nGroup ) {
			for ( int nDepth = 0; nDepth < 64; nDepth++ )
			{
				const auto it = parents.find( nGroup );
				if ( it == parents.end() || it->second == 0 || it->second == nGroup )
				{
					break;
				}
				nGroup = it->second;
			}
			return nGroup;
		};
		std::set<int> groups;
		for ( const std::uint32_t n : objects )
		{
			if ( const int nGroup = m_pScene->GetObjectEditor( n ).m_nGroup; nGroup != 0 )
			{
				groups.insert( topGroup( nGroup ) );
			}
		}
		if ( !groups.empty() )
		{
			for ( std::uint32_t n = 0; n < m_pScene->GetObjectCount(); n++ )
			{
				const int nGroup = m_pScene->GetObjectEditor( n ).m_nGroup;
				if ( nGroup != 0 && groups.contains( topGroup( nGroup ) ) )
				{
					objects.push_back( n );
					withEntity( n, objects );
				}
			}
		}
	}
	SortUnique( objects );
	return objects;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::Box_t CMapDocument::GetObjectBounds( std::uint32_t nObject ) const
{
	if ( !m_pScene || nObject >= m_pScene->GetObjectCount() )
	{
		return {};
	}
	if ( m_pScene->IsSolidObject( nObject ) )
	{
		return m_pScene->m_Solids[ nObject ].m_Bounds;
	}
	const std::size_t nEntity = nObject - m_pScene->m_Solids.size();
	if ( m_pScene->m_Entities[ nEntity ].m_bPoint )
	{
		return m_pScene->GetEntityBox( nEntity );
	}
	xv::map::Box_t box;
	for ( const std::uint32_t nSolid : m_pScene->m_Map.m_Entities[ nEntity ].m_Solids )
	{
		box.Add( m_pScene->m_Solids[ nSolid ].m_Bounds );
	}
	return box;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::Box_t CMapDocument::GetSelectionBounds() const
{
	xv::map::Box_t box;
	for ( const std::uint32_t n : m_Selection )
	{
		box.Add( GetObjectBounds( n ) );
	}
	return box;
}
