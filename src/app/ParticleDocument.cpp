//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ParticleDocument.h.
//
//===========================================================================//
#include "ParticleDocument.h"

#include <QFileInfo>
#include <QHash>
#include <QUndoStack>

#include "particles/Particles.h"

using xv::dmx::CElement;
using xv::dmx::Document_t;

namespace
{

struct ElementChange_t
{
	std::size_t m_nIndex = 0;
	std::optional<CElement> m_Before; // nullopt: the element didn't exist yet
	std::optional<CElement> m_After;  // nullopt: the element was removed
};

struct CHeader
{
	std::string m_szEncoding;
	int m_nEncodingVersion = 0;
	std::string m_szFormat;
	int m_nFormatVersion = 0;

	bool operator==( const CHeader & ) const = default;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CHeader HeaderOf( const Document_t &document )
{
	return { document.m_szEncoding, document.m_nEncodingVersion, document.m_szFormat, document.m_nFormatVersion };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void ApplyHeader( Document_t &document, const CHeader &header )
{
	document.m_szEncoding = header.m_szEncoding;
	document.m_nEncodingVersion = header.m_nEncodingVersion;
	document.m_szFormat = header.m_szFormat;
	document.m_nFormatVersion = header.m_nFormatVersion;
}

} // namespace

class CParticleEditCommand : public QUndoCommand
{
public:
	CParticleEditCommand( CParticleDocument *pDocument, const QString &szText, const Document_t &before, const QString &szMergeKey )
		: QUndoCommand( szText ),
		  m_pDocument( pDocument ),
		  m_nMergeId( szMergeKey.isEmpty() ? -1 : static_cast<int>( qHash( szMergeKey ) & 0x7fffffff ) ),
		  m_szMergeKey( szMergeKey )
	{
		const Document_t &after = pDocument->m_Document;
		m_nOldSize = before.m_Elements.size();
		m_nNewSize = after.m_Elements.size();
		m_HeaderBefore = HeaderOf( before );
		m_HeaderAfter = HeaderOf( after );
		for ( std::size_t i = 0; i < std::max( m_nOldSize, m_nNewSize ); i++ )
		{
			const CElement *b = i < m_nOldSize ? &before.m_Elements[ i ] : nullptr;
			const CElement *a = i < m_nNewSize ? &after.m_Elements[ i ] : nullptr;
			if ( !b || !a || !xv::dmx::SameElement( *b, *a ) )
			{
				m_Changes.push_back( { i, b ? std::optional<CElement>{ *b } : std::nullopt, a ? std::optional<CElement>{ *a } : std::nullopt } );
			}
		}
	}

	[[nodiscard]] bool Empty() const { return m_Changes.empty() && m_HeaderBefore == m_HeaderAfter; }

	// The edit is already applied when the command is pushed; the first redo() has nothing to do.
	void redo() override
	{
		if ( m_bFirst )
		{
			m_bFirst = false;
			return;
		}
		Document_t &doc = m_pDocument->m_Document;
		doc.m_Elements.resize( m_nNewSize );
		for ( const ElementChange_t &change : m_Changes )
		{
			if ( change.m_After )
			{
				doc.m_Elements[ change.m_nIndex ] = *change.m_After;
			}
		}
		ApplyHeader( doc, m_HeaderAfter );
		emit m_pDocument->Changed();
	}

	void undo() override
	{
		Document_t &doc = m_pDocument->m_Document;
		doc.m_Elements.resize( std::max( m_nOldSize, m_nNewSize ) );
		for ( const ElementChange_t &change : m_Changes )
		{
			if ( change.m_Before )
			{
				doc.m_Elements[ change.m_nIndex ] = *change.m_Before;
			}
		}
		doc.m_Elements.resize( m_nOldSize );
		ApplyHeader( doc, m_HeaderBefore );
		emit m_pDocument->Changed();
	}

	[[nodiscard]] int id() const override { return m_nMergeId; }

	bool mergeWith( const QUndoCommand *pOther ) override
	{
		const auto *pNext = static_cast<const CParticleEditCommand *>( pOther );
		if ( pNext->m_szMergeKey != m_szMergeKey )
		{
			return false;
		}
		for ( const ElementChange_t &change : pNext->m_Changes )
		{
			const auto mine = std::ranges::find( m_Changes, change.m_nIndex, &ElementChange_t::m_nIndex );
			if ( mine == m_Changes.end() )
			{
				m_Changes.push_back( change );
			}
			else
			{
				mine->m_After = change.m_After;
			}
		}
		m_nNewSize = pNext->m_nNewSize;
		m_HeaderAfter = pNext->m_HeaderAfter;
		return true;
	}

private:
	CParticleDocument *m_pDocument;
	int m_nMergeId;
	QString m_szMergeKey;
	std::vector<ElementChange_t> m_Changes;
	std::size_t m_nOldSize = 0;
	std::size_t m_nNewSize = 0;
	CHeader m_HeaderBefore;
	CHeader m_HeaderAfter;
	bool m_bFirst = true;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleDocument::CParticleDocument( QString szGamePath, Document_t document, QObject *pParent )
	: QObject( pParent ),
	  m_szGamePath( std::move( szGamePath ) ),
	  m_Document( std::move( document ) ),
	  m_pUndo( new QUndoStack( this ) )
{
	connect( m_pUndo, &QUndoStack::cleanChanged, this, [ this ] { emit ModifiedChanged( IsModified() ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CParticleDocument::GetFileName() const
{
	return QFileInfo{ m_szGamePath }.fileName();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CParticleDocument::IsModified() const
{
	return m_bNeverSaved || !m_pUndo->isClean();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleDocument::MarkSaved()
{
	m_bNeverSaved = false;
	m_pUndo->setClean();
	emit ModifiedChanged( IsModified() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleDocument::MarkUnsaved()
{
	m_bNeverSaved = true;
	emit ModifiedChanged( true );
}

//-----------------------------------------------------------------------------
// Purpose: The file as written: binary, without elements nothing refers to any more. An unedited
//          file
//          comes out byte for byte as it was read.
//-----------------------------------------------------------------------------
std::optional<xv::dmx::Bytes_t> CParticleDocument::Serialize( std::string *pError ) const
{
	return xv::dmx::WriteBinary( xv::particles::Compact( m_Document ), pError );
}

//-----------------------------------------------------------------------------
// Purpose: Applies change to the document as one undo step named description. Nothing is recorded
//          when
//          it changes nothing. Consecutive edits with the same non-empty mergeKey (typing into one
//          field)
//          become a single step.
//-----------------------------------------------------------------------------
bool CParticleDocument::Edit( const QString &szDescription, const std::function<void( Document_t & )> &pfnChange, const QString &szMergeKey )
{
	const Document_t before = m_Document;
	pfnChange( m_Document );
	auto *pCommand = new CParticleEditCommand( this, szDescription, before, szMergeKey );
	if ( pCommand->Empty() )
	{
		delete pCommand;
		return false;
	}
	m_pUndo->push( pCommand );
	emit Changed();
	return true;
}
