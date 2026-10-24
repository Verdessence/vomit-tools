//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See TextureDocument.h.
//
//===========================================================================//
#include "TextureDocument.h"

#include <QFileInfo>
#include <QUndoCommand>
#include <QUndoStack>

class CReplaceTextureCommand : public QUndoCommand
{
public:
	CReplaceTextureCommand( CTextureDocument *pDocument, std::vector<std::byte> before, std::vector<std::byte> after, const QString &szText )
		: QUndoCommand( szText ),
		  m_pDocument( pDocument ),
		  m_Before( std::move( before ) ),
		  m_After( std::move( after ) )
	{}

	void redo() override { m_pDocument->Set( m_After ); }
	void undo() override { m_pDocument->Set( m_Before ); }

private:
	CTextureDocument *m_pDocument;
	std::vector<std::byte> m_Before;
	std::vector<std::byte> m_After;
};

//-----------------------------------------------------------------------------
// Purpose: bytes must read as a VTF (see IsValid).
//-----------------------------------------------------------------------------
CTextureDocument::CTextureDocument( QString szGamePath, std::vector<std::byte> bytes, QObject *pParent )
	: QObject( pParent ),
	  m_szGamePath( std::move( szGamePath ) ),
	  m_pUndo( new QUndoStack( this ) )
{
	if ( auto vtf = xv::texture::ReadVtf( bytes ) )
	{
		m_Vtf = std::move( *vtf );
		m_bValid = true;
	}
	connect( m_pUndo, &QUndoStack::cleanChanged, this, &CTextureDocument::UpdateModified );
	connect( m_pUndo, &QUndoStack::indexChanged, this, &CTextureDocument::UpdateModified );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CTextureDocument::GetFileName() const
{
	return QFileInfo{ m_szGamePath }.fileName();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTextureDocument::IsModified() const
{
	return m_bNeverSaved || !m_pUndo->isClean();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureDocument::MarkSaved()
{
	m_bNeverSaved = false;
	m_pUndo->setClean();
	m_bWasModified = false;
	emit ModifiedChanged( false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureDocument::MarkUnsaved()
{
	m_bNeverSaved = true;
	m_bWasModified = true;
	emit ModifiedChanged( true );
}

//-----------------------------------------------------------------------------
// Purpose: Puts a new file in place of the current one as one undo step.
//-----------------------------------------------------------------------------
bool CTextureDocument::Replace( std::vector<std::byte> bytes, const QString &szDescription )
{
	if ( !xv::texture::ReadVtf( bytes ) )
	{
		return false;
	}
	m_pUndo->push( new CReplaceTextureCommand( this, m_Vtf.m_Data, std::move( bytes ), szDescription ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Bytes from Replace or the undo stack, which have read as a VTF before.
//-----------------------------------------------------------------------------
void CTextureDocument::Set( const std::vector<std::byte> &bytes )
{
	if ( auto vtf = xv::texture::ReadVtf( bytes ) )
	{
		m_Vtf = std::move( *vtf );
		m_bValid = true;
	}
	emit Changed();
}

//-----------------------------------------------------------------------------
// Purpose: Tells the views when the modified state changes.
//-----------------------------------------------------------------------------
void CTextureDocument::UpdateModified()
{
	const bool bModified = IsModified();
	if ( bModified != m_bWasModified )
	{
		m_bWasModified = bModified;
		emit ModifiedChanged( bModified );
	}
}
