//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ModelDocument.h.
//
//===========================================================================//
#include "ModelDocument.h"

#include <QUndoCommand>
#include <QUndoStack>

#include "model/Mdl.h"
#include "model/Phy.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Serves the set's own files and nothing else, for checking it reads on its own.
//-----------------------------------------------------------------------------
xv::model::Reader_t SetReader( const CModelDocument &document )
{
	return [ &document ]( std::string_view szPath ) { return document.Read( szPath ); };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: One edit: the runs of bytes it changed, both ways.
//-----------------------------------------------------------------------------
class CModelChangeCommand : public QUndoCommand
{
public:
	CModelChangeCommand( CModelDocument *pDocument, const QString &szName, xv::model::Change_t change, int nMerge )
		: QUndoCommand( szName ),
		  m_pDocument( pDocument ),
		  m_Change( std::move( change ) ),
		  m_nMerge( nMerge )
	{
	}

	void redo() override { m_pDocument->Apply( m_Change, true ); }
	void undo() override { m_pDocument->Apply( m_Change, false ); }
	[[nodiscard]] int id() const override { return m_nMerge; }

	// Both changes are applied now: the joined one is from before this to after the other.
	bool mergeWith( const QUndoCommand *pOther ) override
	{
		const auto *pNext = static_cast<const CModelChangeCommand *>( pOther );
		xv::model::Files_t before = m_pDocument->m_Files;
		xv::model::ApplyChange( before, pNext->m_Change, false );
		xv::model::ApplyChange( before, m_Change, false );
		m_Change = xv::model::Diff( before, m_pDocument->m_Files );
		return true;
	}

private:
	CModelDocument *m_pDocument;
	xv::model::Change_t m_Change;
	int m_nMerge;
};

//-----------------------------------------------------------------------------
// Purpose: Notes whether the set draws and its hull reads, so saving can insist they still do.
//-----------------------------------------------------------------------------
CModelDocument::CModelDocument( std::string szMdlPath, xv::model::Files_t files, std::map<std::string, std::string> paths, QObject *pParent )
	: QObject( pParent ),
	  m_szMdlPath( std::move( szMdlPath ) ),
	  m_Files( std::move( files ) ),
	  m_Paths( std::move( paths ) ),
	  m_pUndo( new QUndoStack( this ) )
{
	m_Saved = m_Files;
	m_bDrewWhenOpened = Has( ".vvd" ) && xv::model::LoadModel( m_szMdlPath, SetReader( *this ) ).has_value();
	m_bHullReadWhenOpened = Has( ".phy" ) && xv::model::ReadPhysics( m_Files.at( ".phy" ) ).has_value();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CModelDocument::PathOf( const std::string &szKind ) const
{
	if ( const auto found = m_Paths.find( szKind ); found != m_Paths.end() )
		return found->second;
	return m_szMdlPath.substr( 0, m_szMdlPath.size() - 4 ) + szKind;
}

//-----------------------------------------------------------------------------
// Purpose: Paths compare as the file system does: case and slashes don't matter.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> CModelDocument::Read( std::string_view szPath ) const
{
	const auto same = []( std::string_view a, std::string_view b ) {
		return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin(), []( char x, char y ) {
			const auto fold = []( char c ) { return c == '\\' ? '/' : static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ); };
			return fold( x ) == fold( y );
		} );
	};
	for ( const auto &[ szKind, bytes ] : m_Files )
		if ( same( szPath, PathOf( szKind ) ) )
			return bytes;
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CModelDocument::IsModified() const
{
	return m_Files != m_Saved;
}

std::vector<std::string> CModelDocument::GetModifiedKinds() const
{
	std::vector<std::string> kinds;
	for ( const auto &[ szKind, bytes ] : m_Files )
		if ( const auto found = m_Saved.find( szKind ); found == m_Saved.end() || found->second != bytes )
			kinds.push_back( szKind );
	return kinds;
}

void CModelDocument::MarkSaved()
{
	m_Saved = m_Files;
	m_pUndo->setClean();
	emit ModifiedChanged( false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CModelDocument::Edit( const QString &szName, const std::function<void( xv::model::Files_t & )> &pfnEdit, int nMerge )
{
	xv::model::Files_t next = m_Files;
	try
	{
		pfnEdit( next );
	}
	catch ( const std::exception &error )
	{
		return QString::fromStdString( error.what() );
	}
	xv::model::Change_t change = xv::model::Diff( m_Files, next );
	if ( change.empty() )
		return {};
	m_pUndo->push( new CModelChangeCommand( this, szName, std::move( change ), nMerge ) );
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CModelDocument::Apply( const xv::model::Change_t &change, bool bForward )
{
	const bool bWasModified = IsModified();
	xv::model::ApplyChange( m_Files, change, bForward );
	emit Changed();
	if ( IsModified() != bWasModified )
		emit ModifiedChanged( !bWasModified );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CModelDocument::Check() const
{
	std::string szError;
	if ( !xv::model::ReadStudio( m_szMdlPath, SetReader( *this ), &szError ) )
		return tr( "The edited model no longer reads: %1" ).arg( QString::fromStdString( szError ) );
	if ( m_bDrewWhenOpened && !xv::model::LoadModel( m_szMdlPath, SetReader( *this ), &szError ) )
		return tr( "The edited model no longer draws: %1" ).arg( QString::fromStdString( szError ) );
	if ( m_bHullReadWhenOpened && !xv::model::ReadPhysics( m_Files.at( ".phy" ), &szError ) )
		return tr( "The edited collision hull no longer reads: %1" ).arg( QString::fromStdString( szError ) );
	return {};
}
