//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See StripperEditor.h.
//
//===========================================================================//
#include "StripperEditor.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "BspViews.h"
#include "FileDialogs.h"
#include "bsp/Stripper.h"
#include "kv1/Kv1.h"
#include "vfs/Vfs.h"

//-----------------------------------------------------------------------------
// Purpose: The bar naming the map, then the entity view editing the config.
//-----------------------------------------------------------------------------
CStripperEditor::CStripperEditor( const std::filesystem::path &config, const std::filesystem::path &map, QWidget *pParent )
	: QWidget( pParent ),
	  m_Config( config )
{
	setObjectName( QStringLiteral( "stripperEditor" ) );
	m_pLayout = new QVBoxLayout( this );
	m_pLayout->setContentsMargins( 0, 6, 0, 0 );
	auto *pBar = new QHBoxLayout;
	pBar->setContentsMargins( 6, 0, 6, 0 );
	m_pMapLabel = new QLabel;
	m_pMapLabel->setObjectName( QStringLiteral( "stripperMap" ) );
	m_pMapLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
	auto *pPick = new QPushButton( tr( "Map…" ) );
	pPick->setObjectName( QStringLiteral( "stripperPickMap" ) );
	pPick->setToolTip( tr( "Preview the config on another map" ) );
	connect( pPick, &QPushButton::clicked, this, &CStripperEditor::PickMap );
	pBar->addWidget( m_pMapLabel, 1 );
	pBar->addWidget( pPick );
	m_pLayout->addLayout( pBar );
	SetMap( map );
}

//-----------------------------------------------------------------------------
// Purpose: A new view on the map read again; text typed and not saved goes over to it.
//-----------------------------------------------------------------------------
void CStripperEditor::SetMap( const std::filesystem::path &map )
{
	const bool bModified = IsModified();
	const QString szText = m_pView ? m_pView->GetConfigText() : QString{};
	m_Map.clear();
	m_Bsp = {};
	QString szProblem;
	if ( !map.empty() )
	{
		std::string szError;
		if ( std::optional<xv::bsp::CBsp> bsp = xv::bsp::CBsp::Load( map, &szError ) )
		{
			m_Bsp = std::move( *bsp );
			m_Map = map;
		}
		else
		{
			szProblem = QString::fromStdString( szError );
		}
	}
	if ( m_pView )
	{
		m_pLayout->removeWidget( m_pView );
		m_pView->deleteLater();
	}
	m_pView = new CEntityView( m_Bsp, m_Map );
	if ( m_bFromGame )
		m_pView->ReadConfigData( m_Config, m_szGameData );
	else
		m_pView->EditConfig( m_Config );
	if ( bModified )
		m_pView->SetConfigText( szText );
	connect( m_pView, &CEntityView::ConfigModifiedChanged, this, &CStripperEditor::ModifiedChanged );
	m_pLayout->addWidget( m_pView, 1 );
	if ( !m_Map.empty() )
		m_pMapLabel->setText( tr( "Previewed on %1" ).arg( QDir::toNativeSeparators( QString::fromStdU16String( m_Map.u16string() ) ) ) );
	else if ( !szProblem.isEmpty() )
		m_pMapLabel->setText( tr( "Couldn't read the map: %1. Only what the config adds is shown." ).arg( szProblem ) );
	else
		m_pMapLabel->setText( tr( "No map found for this config (addons/stripper/maps/<map>.cfg looks in the game's maps folders); only what it adds is shown." ) );
	if ( bModified != IsModified() )
		emit ModifiedChanged( IsModified() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CStripperEditor::PickMap()
{
	const QString szStart = !m_Map.empty() ? QString::fromStdU16String( m_Map.parent_path().u16string() ) : QString{};
	const QString szFile = AskOpenFile( this, tr( "Map to Preview On" ), szStart, tr( "Maps (*.bsp);;All files (*)" ) );
	if ( !szFile.isEmpty() )
		SetMap( std::filesystem::path( szFile.toStdU16String() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CStripperEditor::IsModified() const
{
	return m_pView && m_pView->IsConfigModified();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CStripperEditor::GetTabTitle() const
{
	return ( IsModified() ? QStringLiteral( "*" ) : QString{} ) + QString::fromStdU16String( m_Config.filename().u16string() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CStripperEditor::GetSessionEntry() const
{
	return QStringLiteral( "stripper\t%1\t%2" ).arg( m_bFromGame ? QStringLiteral( "game" ) : QStringLiteral( "disk" ), QString::fromStdU16String( m_Config.u16string() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Save, drop the changes, or keep the tab.
//-----------------------------------------------------------------------------
bool CStripperEditor::ConfirmClose()
{
	if ( !IsModified() )
		return true;
	const auto nAnswer = QMessageBox::question( this, tr( "Unsaved Stripper Config" ), tr( "Save the changes to %1?" ).arg( QString::fromStdU16String( m_Config.filename().u16string() ) ), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );
	if ( nAnswer == QMessageBox::Save )
		return Save();
	return nAnswer == QMessageBox::Discard;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CStripperEditor::Save()
{
	return m_pView && m_pView->SaveConfig();
}

//-----------------------------------------------------------------------------
// Purpose: The folder settles it when it says so; otherwise a rule read from the text.
//-----------------------------------------------------------------------------
bool CStripperEditor::IsStripperConfig( const std::filesystem::path &file, std::string_view szText )
{
	if ( !xv::kv1::EqualsIgnoreCase( xv::vfs::ToUtf8( file.extension() ), ".cfg" ) )
		return false;
	for ( std::filesystem::path folder = file.parent_path(); !folder.empty() && folder != folder.parent_path(); folder = folder.parent_path() )
	{
		if ( xv::kv1::EqualsIgnoreCase( xv::vfs::ToUtf8( folder.filename() ), "stripper" ) && xv::kv1::EqualsIgnoreCase( xv::vfs::ToUtf8( folder.parent_path().filename() ), "addons" ) )
			return true;
	}
	return !xv::bsp::ParseStripper( szText, "" ).m_Rules.empty();
}

//-----------------------------------------------------------------------------
// Purpose: Keep archived text in the tab when the preview map changes.
//-----------------------------------------------------------------------------
void CStripperEditor::SetGameData( const QString &szText )
{
	m_bFromGame = true;
	m_szGameData = szText;
	m_pView->ReadConfigData( m_Config, szText );
}

//-----------------------------------------------------------------------------
// Purpose: Save a separate loose config.
//-----------------------------------------------------------------------------
bool CStripperEditor::SaveAs()
{
	return m_pView && m_pView->SaveConfigAs();
}
