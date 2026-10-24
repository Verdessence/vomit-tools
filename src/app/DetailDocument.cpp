//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DetailDocument.h.
//===========================================================================//
#include "DetailDocument.h"
#include <QFileInfo>

class CDetailEditCommand : public QUndoCommand
{
public:
	//-----------------------------------------------------------------------------
	// Purpose: Keep the complete before and after token trees for one atomic edit.
	//-----------------------------------------------------------------------------
	CDetailEditCommand( CDetailDocument *pDocument, xv::kv1::CDocument after, const QString &szDescription, std::function<void( bool )> pfnContext )
		: QUndoCommand( szDescription ),
		  m_pDocument( pDocument ),
		  m_Before( pDocument->m_Document ),
		  m_After( std::move( after ) ),
		  m_pfnContext( std::move( pfnContext ) )
	{}
	//-----------------------------------------------------------------------------
	// Purpose: Restore the original tokens exactly.
	//-----------------------------------------------------------------------------
	void undo() override
	{
		m_pDocument->m_Document = m_Before;
		if ( m_pfnContext )
		{
			m_pfnContext( false );
		}
		emit m_pDocument->Changed();
	}
	//-----------------------------------------------------------------------------
	// Purpose: Apply the edited tokens.
	//-----------------------------------------------------------------------------
	void redo() override
	{
		m_pDocument->m_Document = m_After;
		if ( m_pfnContext )
		{
			m_pfnContext( true );
		}
		emit m_pDocument->Changed();
	}

private:
	CDetailDocument *m_pDocument;
	xv::kv1::CDocument m_Before;
	xv::kv1::CDocument m_After;
	std::function<void( bool )> m_pfnContext;
};

//-----------------------------------------------------------------------------
// Purpose: Own the parsed tokens and their undo history.
//-----------------------------------------------------------------------------
CDetailDocument::CDetailDocument( QString szGamePath, xv::kv1::CParseResult parsed, QObject *pParent )
	: QObject( pParent ),
	  m_Document( std::move( parsed.m_Document ) ),
	  m_Diagnostics( std::move( parsed.m_Diagnostics ) ),
	  m_szGamePath( std::move( szGamePath ) ),
	  m_pUndo( new QUndoStack( this ) )
{
	connect( m_pUndo, &QUndoStack::cleanChanged, this, [ this ] { emit ModifiedChanged( IsModified() ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Name the document in tabs and save dialogs.
//-----------------------------------------------------------------------------
QString CDetailDocument::GetFileName() const
{
	return m_DiskPath.empty() ? QFileInfo( m_szGamePath ).fileName() : QString::fromStdU16String( m_DiskPath.filename().u16string() );
}

//-----------------------------------------------------------------------------
// Purpose: Record that the current history position was saved successfully.
//-----------------------------------------------------------------------------
void CDetailDocument::MarkSaved()
{
	m_bNew = false;
	m_pUndo->setClean();
	emit ModifiedChanged( false );
}

//-----------------------------------------------------------------------------
// Purpose: Keep a new document dirty even before its first edit.
//-----------------------------------------------------------------------------
void CDetailDocument::MarkUnsaved()
{
	m_bNew = true;
	emit ModifiedChanged( true );
}

//-----------------------------------------------------------------------------
// Purpose: Commit an atomic token edit; snapshots restore comments and layout exactly.
//-----------------------------------------------------------------------------
void CDetailDocument::Edit( const QString &szDescription, const std::function<void( xv::kv1::CDocument & )> &pfnEdit, std::function<void( bool )> pfnContext )
{
	auto after = m_Document;
	pfnEdit( after );
	if ( xv::kv1::Write( after ) != GetSerialize() )
	{
		m_pUndo->push( new CDetailEditCommand( this, std::move( after ), szDescription, std::move( pfnContext ) ) );
	}
}
