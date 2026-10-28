//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapDocument.h.
//
//===========================================================================//
#include "MapDocument.h"

#include "map/Cordon.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <unordered_map>

#include <QFile>
#include <QSaveFile>
#include <QUndoStack>

#include "kv1/Kv1.h"
#include "map/GameConfig.h"
#include "map/Vmf.h"
#include "map/Write.h"

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

// One edit on the undo stack: the objects as the edit makes them, and as they were.
class CMapEditCommand : public QUndoCommand
{
public:
	CMapEditCommand( CMapDocument &document, xv::map::MapEdit_t forward, xv::map::MapEdit_t back, std::vector<std::uint32_t> selectionBefore, std::optional<std::vector<std::uint32_t>> selectionAfter, const QString &szName )
		: QUndoCommand( szName ),
		  m_Document( document ),
		  m_Forward( std::move( forward ) ),
		  m_Back( std::move( back ) ),
		  m_SelectionBefore( std::move( selectionBefore ) ),
		  m_SelectionAfter( std::move( selectionAfter ) )
	{
	}

	void redo() override
	{
		m_Document.ApplyEditNow( m_Forward );
		if ( m_SelectionAfter )
		{
			m_Document.SetSelection( *m_SelectionAfter );
		}
	}

	void undo() override
	{
		m_Document.ApplyEditNow( m_Back );
		m_Document.SetSelection( m_SelectionBefore );
	}

private:
	CMapDocument &m_Document;
	xv::map::MapEdit_t m_Forward;
	xv::map::MapEdit_t m_Back;
	std::vector<std::uint32_t> m_SelectionBefore;
	std::optional<std::vector<std::uint32_t>> m_SelectionAfter;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: gameinfo.txt's InstancePath, under the game's folder, when it has one.
//-----------------------------------------------------------------------------
std::optional<std::filesystem::path> GameInstancePath( const xv::vfs::CGameInfo &game )
{
	std::ifstream file( game.m_File, std::ios::binary );
	const std::string szText( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
	const xv::kv1::CParseResult result = xv::kv1::Parse( szText );
	const xv::kv1::CNode *pGameInfo = result.m_Document.Find( "GameInfo" );
	const xv::kv1::CNode *pPath = pGameInfo ? pGameInfo->Find( "InstancePath" ) : nullptr;
	if ( !pPath || pPath->GetValue().empty() )
	{
		return std::nullopt;
	}
	return game.m_GameDir / std::filesystem::path{ pPath->GetValue() };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapDocument::CMapDocument( QObject *pParent )
	: QObject( pParent ),
	  m_pUndo( new QUndoStack( this ) )
{
	connect( m_pUndo, &QUndoStack::cleanChanged, this, [ this ]( bool bClean ) { emit ModifiedChanged( !bClean ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapDocument::IsModified() const
{
	return !m_pUndo->isClean();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::Edit( xv::map::MapEdit_t edit, const QString &szName, std::optional<std::vector<std::uint32_t>> selectAfter )
{
	if ( !m_pScene || edit.IsEmpty() )
	{
		return;
	}
	xv::map::MapEdit_t back = xv::map::InverseOf( *m_pScene, edit );
	m_pUndo->push( new CMapEditCommand( *this, std::move( edit ), std::move( back ), m_Selection, std::move( selectAfter ), szName ) );
}

//-----------------------------------------------------------------------------
// Purpose: The new snapshot, and the views' state for the objects it adds (shown, as new
//          objects are) or deletes (no longer selected).
//-----------------------------------------------------------------------------
void CMapDocument::ApplyEditNow( const xv::map::MapEdit_t &edit )
{
	if ( !m_pScene )
	{
		return;
	}
	std::shared_ptr<const xv::map::CScene> pScene = xv::map::ApplyEdit( *m_pScene, edit );
	const std::uint32_t nObjects = pScene->GetObjectCount();
	for ( std::uint32_t n = static_cast<std::uint32_t>( m_UserShown.size() ); n < nObjects; n++ )
	{
		const xv::map::EditorInfo_t &editor = pScene->GetObjectEditor( n );
		m_UserShown.push_back( editor.m_bVisgroupShown ? 1 : 0 );
		m_AutoShown.push_back( editor.m_bAutoShown ? 1 : 0 );
		m_QuickHidden.push_back( 0 );
	}
	m_pScene = std::move( pScene );
	UpdateCordonHidden();
	std::erase_if( m_Selection, [ this ]( std::uint32_t n ) { return m_pScene->IsDeleted( n ); } );
	m_nStateGeneration++;
	emit SceneEdited( m_pScene->m_Changed );
	emit SelectionChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::Preview( const xv::map::MapEdit_t &edit )
{
	if ( !m_pScene )
	{
		return;
	}
	if ( !m_pPreviewBase )
	{
		m_pPreviewBase = m_pScene;
	}
	m_pScene = m_pPreviewBase;
	ApplyEditNow( edit );
}

//-----------------------------------------------------------------------------
// Purpose: The objects the preview changed are drawn again as they were.
//-----------------------------------------------------------------------------
void CMapDocument::EndPreview()
{
	if ( !m_pPreviewBase )
	{
		return;
	}
	const std::vector<std::uint32_t> changed = m_pScene->m_Changed;
	m_pScene = std::move( m_pPreviewBase );
	UpdateCordonHidden();
	m_nStateGeneration++;
	emit SceneEdited( changed );
}

//-----------------------------------------------------------------------------
// Purpose: Through a temporary file, so a failed write leaves the map as it was.
//-----------------------------------------------------------------------------
bool CMapDocument::Save( const std::filesystem::path &path, QString *pError )
{
	if ( !m_pScene )
	{
		return false;
	}
	const std::string szText = xv::map::WriteVmf( m_pScene->m_Map );
	const QString szPath = QString::fromStdString( path.string() );
	if ( QFile::exists( szPath ) )
	{
		// Hammer's backup of the map as it was before this save.
		std::filesystem::path backup = path;
		backup.replace_extension( ".vmx" );
		const QString szBackup = QString::fromStdString( backup.string() );
		QFile::remove( szBackup );
		QFile::copy( szPath, szBackup );
	}
	QSaveFile file( szPath );
	if ( !file.open( QIODevice::WriteOnly ) || file.write( szText.data(), static_cast<qint64>( szText.size() ) ) != static_cast<qint64>( szText.size() ) || !file.commit() )
	{
		if ( pError )
		{
			*pError = file.errorString();
		}
		return false;
	}
	m_pUndo->setClean();
	return true;
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
	// The game's Hammer configuration also says what new objects get, whichever FGDs are read.
	xv::map::HammerDefaults_t defaults;
	if ( source.m_Game )
	{
		xv::map::GameFgds_t found = xv::map::FindGameFgds( source.m_Game->m_GameDir );
		defaults = std::move( found.m_Defaults );
		if ( fgds.empty() )
		{
			fgds = std::move( found.m_Files );
			szFgdSource = QString::fromStdString( found.m_szSource );
		}
	}
	if ( fgds.empty() )
	{
		szFgdSource.clear();
	}
	std::shared_ptr<const xv::map::CFgd> pFgd = xv::map::LoadFgds( fgds );

	xv::map::SceneOptions_t options;
	options.m_pCancel = &m_bStop;
	options.m_pfnProgress = progress;
	// func_instances' files are found from the map's own folder, then from the game's
	// InstancePath (a map read out of the game has no folder of its own).
	options.m_Instances.m_MapFile = source.m_DiskPath;
	if ( source.m_Game )
	{
		if ( const std::optional<std::filesystem::path> path = GameInstancePath( *source.m_Game ) )
		{
			options.m_Instances.m_SearchPaths.push_back( *path );
		}
	}
	std::shared_ptr<xv::map::CScene> pScene = xv::map::BuildScene( std::move( *map ), std::move( pFgd ), options );
	if ( !pScene )
	{
		return;
	}
	post( [ this, pScene = std::move( pScene ), szFgdSource, defaults = std::move( defaults ) ]() mutable { Finish( std::move( pScene ), szFgdSource, std::move( defaults ) ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapDocument::SetPointfile( std::optional<std::vector<xv::map::Point_t>> points )
{
	m_Pointfile = std::move( points );
	m_nStateGeneration++;
	emit PointfileChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The map is built: what starts hidden follows the flags Hammer saved.
//-----------------------------------------------------------------------------
void CMapDocument::Finish( std::shared_ptr<xv::map::CScene> pScene, QString szFgdSource, xv::map::HammerDefaults_t defaults )
{
	m_szFgdSource = std::move( szFgdSource );
	m_Defaults = std::move( defaults );
	m_nLoads++;
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
	m_pUndo->clear();
	m_pScene = std::move( pScene );
	UpdateCordonHidden();
	m_nStateGeneration++;
	emit Loaded();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer hides what the cordon leaves out while it is on; a scene that doesn't need
//          it keeps no flags at all.
//-----------------------------------------------------------------------------
void CMapDocument::UpdateCordonHidden()
{
	m_CordonHidden.clear();
	if ( !m_pScene || !m_pScene->m_Map.m_bCordonsActive )
	{
		return;
	}
	const std::vector<std::uint32_t> outside = xv::map::ObjectsOutsideCordon( *m_pScene );
	if ( !outside.empty() )
	{
		m_CordonHidden.assign( m_pScene->GetObjectCount(), 0 );
		for ( const std::uint32_t n : outside )
		{
			m_CordonHidden[ n ] = 1;
		}
	}
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
	if ( !m_pScene || nObject >= m_UserShown.size() || m_pScene->IsDeleted( nObject ) )
	{
		return false;
	}
	auto own = [ this ]( std::uint32_t n ) { return m_UserShown[ n ] && m_AutoShown[ n ] && !m_QuickHidden[ n ] && !( n < m_CordonHidden.size() && m_CordonHidden[ n ] ); };
	if ( !own( nObject ) )
	{
		return false;
	}
	if ( m_pScene->IsSolidObject( nObject ) )
	{
		const int nEntity = m_pScene->m_Map.m_Solids[ m_pScene->GetIndex( nObject ) ].m_nEntity;
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
			into.push_back( m_pScene->SolidObject( nSolid ) );
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
				if ( nGroup != 0 && !m_pScene->IsDeleted( n ) && groups.contains( topGroup( nGroup ) ) )
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
		return m_pScene->m_Solids[ m_pScene->GetIndex( nObject ) ].m_Bounds;
	}
	const std::size_t nEntity = m_pScene->GetIndex( nObject );
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
