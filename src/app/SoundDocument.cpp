//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundDocument.h.
//
//===========================================================================//
#include "SoundDocument.h"

#include <QFileInfo>
#include <QUndoStack>

using xv::kv1::CNode;
using xv::sound::NodePath_t;

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The node at path inside a copy of its top-level entry (path[0] names the entry itself).
//-----------------------------------------------------------------------------
CNode *Descend( CNode &entry, const NodePath_t &path )
{
	CNode *pNode = &entry;
	for ( std::size_t i = 1; i < path.size(); i++ )
	{
		if ( !pNode->IsBlock() || path[ i ] >= pNode->GetChildren().size() )
		{
			return nullptr;
		}
		pNode = &pNode->Child( path[ i ] );
	}
	return pNode;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
NodePath_t ParentOf( NodePath_t path )
{
	path.pop_back();
	return path;
}

} // namespace

class CReplaceEntryCommand : public QUndoCommand
{
public:
	CReplaceEntryCommand( CSoundDocument *pDocument, std::size_t nIndex, CNode before, CNode after, const QString &szText, QString szMergeKey )
		: QUndoCommand( szText ),
		  m_pDocument( pDocument ),
		  m_nIndex( nIndex ),
		  m_Before( std::move( before ) ),
		  m_After( std::move( after ) ),
		  m_szMergeKey( std::move( szMergeKey ) )
	{}

	[[nodiscard]] int id() const override { return m_szMergeKey.isEmpty() ? -1 : 1; }

	bool mergeWith( const QUndoCommand *pOther ) override
	{
		const auto *pNext = static_cast<const CReplaceEntryCommand *>( pOther );
		if ( pNext->m_nIndex != m_nIndex || pNext->m_szMergeKey != m_szMergeKey )
		{
			return false;
		}
		m_After = pNext->m_After;
		return true;
	}

	void redo() override
	{
		m_pDocument->m_Document.m_Roots[ m_nIndex ] = m_After;
		emit m_pDocument->Changed();
	}

	void undo() override
	{
		m_pDocument->m_Document.m_Roots[ m_nIndex ] = m_Before;
		emit m_pDocument->Changed();
	}

private:
	CSoundDocument *m_pDocument;
	std::size_t m_nIndex;
	CNode m_Before;
	CNode m_After;
	QString m_szMergeKey;
};

class CInsertEntryCommand : public QUndoCommand
{
public:
	CInsertEntryCommand( CSoundDocument *pDocument, std::size_t nIndex, CNode entry, const QString &szText )
		: QUndoCommand( szText ),
		  m_pDocument( pDocument ),
		  m_nIndex( nIndex ),
		  m_Entry( std::move( entry ) )
	{}

	void redo() override
	{
		auto &doc = m_pDocument->m_Document;
		// The first time, lay it out like its neighbours; afterwards reuse exactly that layout.
		if ( !m_bLaidOut )
		{
			m_Entry = doc.InsertRoot( m_nIndex, m_Entry );
			m_bLaidOut = true;
		}
		else
		{
			doc.InsertRoot( m_nIndex, m_Entry, xv::kv1::Layout_t::LAYOUT_KEEP );
		}
		emit m_pDocument->Changed();
	}

	void undo() override
	{
		m_pDocument->m_Document.TakeRoot( m_nIndex );
		emit m_pDocument->Changed();
	}

private:
	CSoundDocument *m_pDocument;
	std::size_t m_nIndex;
	CNode m_Entry;
	bool m_bLaidOut = false;
};

class CRemoveEntryCommand : public QUndoCommand
{
public:
	CRemoveEntryCommand( CSoundDocument *pDocument, std::size_t nIndex, const QString &szText )
		: QUndoCommand( szText ),
		  m_pDocument( pDocument ),
		  m_nIndex( nIndex )
	{}

	void redo() override
	{
		m_Removed = m_pDocument->m_Document.TakeRoot( m_nIndex );
		emit m_pDocument->Changed();
	}

	void undo() override
	{
		m_pDocument->m_Document.InsertRoot( m_nIndex, m_Removed, xv::kv1::Layout_t::LAYOUT_KEEP );
		emit m_pDocument->Changed();
	}

private:
	CSoundDocument *m_pDocument;
	std::size_t m_nIndex;
	CNode m_Removed;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundDocument::CSoundDocument( QString szGamePath, xv::sound::FileKind_t nKind, xv::kv1::CParseResult parsed, QObject *pParent )
	: QObject( pParent ),
	  m_szGamePath( std::move( szGamePath ) ),
	  m_nKind( nKind ),
	  m_pSchema( xv::sound::SchemaFor( nKind ) ),
	  m_Document( std::move( parsed.m_Document ) ),
	  m_Diagnostics( std::move( parsed.m_Diagnostics ) ),
	  m_pUndo( new QUndoStack( this ) )
{
	Q_ASSERT( m_pSchema );
	connect( m_pUndo, &QUndoStack::cleanChanged, this, [ this ]( bool bClean ) {
		emit ModifiedChanged( !bClean || m_bNeverSaved );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CSoundDocument::GetFileName() const
{
	return QFileInfo{ m_szGamePath }.fileName();
}

//-----------------------------------------------------------------------------
// Purpose: The game or mod folder holding this file's scripts/ (or maps/) folder, for finding its
//          sounds on
//          disk when no game is mounted; empty if it isn't in one.
//-----------------------------------------------------------------------------
std::filesystem::path CSoundDocument::GetLocalRoot() const
{
	if ( m_DiskPath.empty() )
	{
		return {};
	}
	const auto folder = m_DiskPath.parent_path();
	const QString szName = QString::fromStdU16String( folder.filename().u16string() );
	if ( szName.compare( u"scripts", Qt::CaseInsensitive ) == 0 || szName.compare( u"maps", Qt::CaseInsensitive ) == 0 )
	{
		return folder.parent_path();
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSoundDocument::IsModified() const
{
	return m_bNeverSaved || !m_pUndo->isClean();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CSoundDocument::GetSerialize() const
{
	return xv::kv1::Write( m_Document );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundDocument::MarkSaved()
{
	m_bNeverSaved = false;
	m_pUndo->setClean();
	emit ModifiedChanged( false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundDocument::MarkUnsaved()
{
	m_bNeverSaved = true;
	emit ModifiedChanged( true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CNode *CSoundDocument::Node( const NodePath_t &path ) const
{
	if ( path.empty() || path[ 0 ] >= m_Document.m_Roots.size() )
	{
		return nullptr;
	}
	return Descend( const_cast<CNode &>( m_Document.m_Roots[ path[ 0 ] ] ), path );
}

//-----------------------------------------------------------------------------
// Purpose: The schema block for the block at path; nullptr for blocks the schema doesn't know.
//-----------------------------------------------------------------------------
const xv::sound::BlockDef_t *CSoundDocument::BlockDef( const NodePath_t &path ) const
{
	if ( path.size() == 1 )
	{
		return m_pSchema->m_pEntry;
	}
	const CNode *pBlock = Node( path );
	return pBlock && pBlock->IsBlock() ? m_pSchema->Block( pBlock->GetKey() ) : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Applies edit to a copy of top-level entry rootIndex and pushes the swap as one undo
//          step.
//-----------------------------------------------------------------------------
void CSoundDocument::EditEntry( std::size_t nRootIndex, const QString &szDescription, const std::function<bool( CNode &entry )> &pfnEdit, const QString &szMergeKey )
{
	if ( nRootIndex >= m_Document.m_Roots.size() )
	{
		return;
	}
	CNode after = m_Document.m_Roots[ nRootIndex ];
	if ( !pfnEdit( after ) )
	{
		return;
	}
	m_pUndo->push( new CReplaceEntryCommand( this, nRootIndex, m_Document.m_Roots[ nRootIndex ], std::move( after ), szDescription, szMergeKey ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundDocument::SetKey( const NodePath_t &path, const QString &szKey )
{
	const CNode *pCurrent = Node( path );
	if ( !pCurrent || pCurrent->GetKey() == szKey.toStdString() )
	{
		return;
	}
	const QString szDescription = path.size() == 1 ? tr( "Rename %1" ).arg( szKey ) : tr( "Change key to %1" ).arg( szKey );
	EditEntry( path[ 0 ], szDescription, [ & ]( CNode &entry ) {
		CNode *pTarget = Descend( entry, path );
		return pTarget && pTarget->SetKey( szKey.toStdString() );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundDocument::SetValue( const NodePath_t &path, const QString &szValue )
{
	const CNode *pCurrent = Node( path );
	if ( !pCurrent || !pCurrent->IsPair() || pCurrent->GetValue() == szValue.toStdString() )
	{
		return;
	}
	// Steps through one value (a spin box's arrows) undo together.
	QString szMergeKey = QStringLiteral( "value" );
	for ( const std::size_t nIndex : path )
	{
		szMergeKey += u'/' + QString::number( nIndex );
	}
	EditEntry(
		path[ 0 ], tr( "Set %1" ).arg( QString::fromStdString( pCurrent->GetKey() ) ),
		[ & ]( CNode &entry ) {
			CNode *pTarget = Descend( entry, path );
			return pTarget && pTarget->SetValue( szValue.toStdString() );
		},
		szMergeKey );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundDocument::InsertChild( const NodePath_t &parent, std::size_t nIndex, CNode child, const QString &szDescription )
{
	if ( parent.empty() )
	{
		return;
	}
	EditEntry( parent[ 0 ], szDescription, [ & ]( CNode &entry ) {
		CNode *pBlock = Descend( entry, parent );
		if ( !pBlock || !pBlock->IsBlock() )
		{
			return false;
		}
		pBlock->InsertChild( nIndex, std::move( child ) );
		return true;
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundDocument::RemoveNode( const NodePath_t &path )
{
	const CNode *pTarget = Node( path );
	if ( !pTarget )
	{
		return;
	}
	const QString szDescription = tr( "Delete %1" ).arg( QString::fromStdString( pTarget->GetKey() ) );
	if ( path.size() == 1 )
	{
		m_pUndo->push( new CRemoveEntryCommand( this, path[ 0 ], szDescription ) );
		return;
	}
	EditEntry( path[ 0 ], szDescription, [ & ]( CNode &entry ) {
		CNode *pParent = Descend( entry, ParentOf( path ) );
		if ( !pParent || !pParent->IsBlock() || path.back() >= pParent->GetChildren().size() )
		{
			return false;
		}
		pParent->RemoveChild( path.back() );
		return true;
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundDocument::InsertEntry( std::size_t nIndex, CNode entry, const QString &szDescription )
{
	m_pUndo->push( new CInsertEntryCommand( this, std::min( nIndex, m_Document.m_Roots.size() ), std::move( entry ), szDescription ) );
}
