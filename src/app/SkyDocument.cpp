//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SkyDocument.h.
//
//===========================================================================//
#include "SkyDocument.h"

#include <utility>

#include <QUndoCommand>
#include <QUndoStack>

using xv::sky::Face_t;

// Swaps all six slots between two states: undo restores every face exactly.
class CSkySlotsCommand : public QUndoCommand
{
public:
	CSkySlotsCommand( CSkyDocument *pDocument, std::array<CSkyDocument::Slot_t, 6> before, std::array<CSkyDocument::Slot_t, 6> after, const QString &szText )
		: QUndoCommand( szText ),
		  m_pDocument( pDocument ),
		  m_Before( std::move( before ) ),
		  m_After( std::move( after ) )
	{}

	void redo() override { m_pDocument->ApplySlots( m_After ); }
	void undo() override { m_pDocument->ApplySlots( m_Before ); }

private:
	CSkyDocument *m_pDocument;
	std::array<CSkyDocument::Slot_t, 6> m_Before;
	std::array<CSkyDocument::Slot_t, 6> m_After;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSkyDocument::CSkyDocument( QObject *pParent )
	: QObject( pParent ),
	  m_pUndoStack( new QUndoStack( this ) )
{
	connect( m_pUndoStack, &QUndoStack::cleanChanged, this, &CSkyDocument::UpdateModified );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSkyDocument::~CSkyDocument() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::SetName( const QString &szName )
{
	if ( szName == m_szName )
	{
		return;
	}
	m_szName = szName;
	SettingChanged();
	emit NameChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::SetFormat( xv::texture::VtfFormat_t nFormat )
{
	if ( nFormat != m_nFormat )
	{
		m_nFormat = nFormat;
		SettingChanged();
		emit OptionsChanged();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::SetSize( int nSize )
{
	if ( nSize != m_nSize )
	{
		m_nSize = nSize;
		SettingChanged();
		emit OptionsChanged();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::SetMipmaps( bool bMipmaps )
{
	if ( bMipmaps != m_bMipmaps )
	{
		m_bMipmaps = bMipmaps;
		SettingChanged();
		emit OptionsChanged();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Undoable. Faces left as nullopt are unchanged; all the changes are one undo step.
//-----------------------------------------------------------------------------
void CSkyDocument::SetSlots( const std::array<std::optional<Slot_t>, 6> &changes, const QString &szText )
{
	std::array<Slot_t, 6> after = m_Slots;
	for ( std::size_t i = 0; i < after.size(); i++ )
	{
		if ( changes[ i ] )
		{
			after[ i ] = *changes[ i ];
		}
	}
	m_pUndoStack->push( new CSkySlotsCommand( this, m_Slots, std::move( after ), szText ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::SetSlot( Face_t nFace, Slot_t slot, const QString &szText )
{
	std::array<std::optional<Slot_t>, 6> changes;
	changes[ xv::sky::IndexOf( nFace ) ] = std::move( slot );
	SetSlots( changes, szText );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::ClearFace( Face_t nFace )
{
	if ( Slot( nFace ).m_pImage )
	{
		SetSlot( nFace, {}, tr( "Clear %1" ).arg( QString::fromUtf8( xv::sky::Suffix( nFace ) ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Turns and/or mirrors a face further (undoable).
//-----------------------------------------------------------------------------
void CSkyDocument::Orient( Face_t nFace, xv::texture::COrientation change, const QString &szText )
{
	Slot_t changed = Slot( nFace );
	if ( !changed.m_pImage )
	{
		return;
	}
	changed.m_Orientation = xv::texture::Combined( changed.m_Orientation, change );
	SetSlot( nFace, std::move( changed ), szText );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::ApplySlots( const std::array<Slot_t, 6> &all )
{
	m_Slots = all;
	m_Cube.reset();
	emit FacesChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The faces as they will be written, for previews, checks and export.
//-----------------------------------------------------------------------------
const xv::sky::CCube &CSkyDocument::GetCube() const
{
	if ( !m_Cube )
	{
		xv::sky::CCube cube;
		for ( Face_t nFace : xv::sky::FACES )
		{
			const Slot_t &s = Slot( nFace );
			if ( s.m_pImage )
			{
				cube.Face( nFace ) = xv::texture::Oriented( *s.m_pImage, s.m_Orientation.m_nQuarterTurns, s.m_Orientation.m_bMirror );
				cube.m_Transforms[ xv::sky::IndexOf( nFace ) ] = s.m_Transform;
			}
		}
		m_Cube = std::move( cube );
	}
	return *m_Cube;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CSkyDocument::GetComplete() const
{
	return std::ranges::all_of( m_Slots, []( const Slot_t &s ) { return s.m_pImage != nullptr; } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::sky::ExportOptions_t CSkyDocument::GetExportOptions() const
{
	return { .m_szName = m_szName.toStdString(), .m_nFormat = m_nFormat, .m_nSize = m_nSize, .m_bMipmaps = m_bMipmaps };
}

//-----------------------------------------------------------------------------
// Purpose: Changed since the last export (or since it was opened).
//-----------------------------------------------------------------------------
bool CSkyDocument::IsModified() const
{
	return m_bSettingsChanged || !m_pUndoStack->isClean();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::MarkExported()
{
	m_bSettingsChanged = false;
	m_pUndoStack->setClean();
	UpdateModified();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::SettingChanged()
{
	m_bSettingsChanged = true;
	UpdateModified();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSkyDocument::UpdateModified()
{
	if ( IsModified() != m_bModified )
	{
		m_bModified = IsModified();
		emit ModifiedChanged( m_bModified );
	}
}
