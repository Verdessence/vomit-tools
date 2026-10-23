//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A sky being made: an image for each face (turned or mirrored as needed), the sky's name
//          and how it's written. Face changes go through the undo stack.
//
//===========================================================================//
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>

#include <QObject>
#include <QString>

#include "sky/Sky.h"
#include "texture/Image.h"
#include "texture/Vtf.h"

class QUndoStack;

class CSkyDocument : public QObject
{
	Q_OBJECT

public:
	struct Slot_t
	{
		std::shared_ptr<const xv::texture::CImage> m_pImage; // as loaded; null for an empty face
		QString m_szSource;                                  // the file or game path it came from
		xv::texture::COrientation m_Orientation;            // applied to image
		xv::sky::CUvTransform m_Transform;                  // from a stock material ($basetexturetransform)
	};

	explicit CSkyDocument( QObject *pParent = nullptr );
	~CSkyDocument() override;

	[[nodiscard]] QString GetName() const { return m_szName; }
	void SetName( const QString &szName );

	[[nodiscard]] const Slot_t &Slot( xv::sky::Face_t nFace ) const { return m_Slots[ xv::sky::IndexOf( nFace ) ]; }
	// Undoable. Faces left as nullopt are unchanged; all the changes are one undo step.
	void SetSlots( const std::array<std::optional<Slot_t>, 6> &changes, const QString &szText );
	void SetSlot( xv::sky::Face_t nFace, Slot_t slot, const QString &szText );
	void ClearFace( xv::sky::Face_t nFace );
	// Turns and/or mirrors a face further (undoable).
	void Orient( xv::sky::Face_t nFace, xv::texture::COrientation change, const QString &szText );

	// The faces as they will be written, for previews, checks and export.
	[[nodiscard]] const xv::sky::CCube &GetCube() const;
	[[nodiscard]] bool GetComplete() const;

	// How it is written: VTF format, face size (0 keeps the images' own) and mipmaps.
	[[nodiscard]] xv::texture::VtfFormat_t GetFormat() const { return m_nFormat; }
	[[nodiscard]] int size() const { return m_nSize; }
	[[nodiscard]] bool GetMipmaps() const { return m_bMipmaps; }
	void SetFormat( xv::texture::VtfFormat_t nFormat );
	void SetSize( int nSize );
	void SetMipmaps( bool bMipmaps );
	[[nodiscard]] xv::sky::ExportOptions_t GetExportOptions() const;

	// Where Export writes when the sky came from a folder on disk: the folder holding its
	// materials/skybox (empty: the game's write folder). The last folder exported to, so replacing
	// files there again needs no confirmation.
	[[nodiscard]] const std::filesystem::path &GetExportFolder() const { return m_ExportFolder; }
	void SetExportFolder( std::filesystem::path folder ) { m_ExportFolder = std::move( folder ); }
	[[nodiscard]] const std::filesystem::path &GetLastExportFolder() const { return m_LastExportFolder; }
	void SetLastExportFolder( std::filesystem::path folder ) { m_LastExportFolder = std::move( folder ); }
	// What the sky was opened from (a folder and base name, or a game sky), to find it among open tabs.
	[[nodiscard]] QString GetOrigin() const { return m_szOrigin; }
	void SetOrigin( const QString &szOrigin ) { m_szOrigin = szOrigin; }

	[[nodiscard]] QUndoStack *GetUndoStack() const { return m_pUndoStack; }
	// Changed since the last export (or since it was opened).
	[[nodiscard]] bool IsModified() const;
	void MarkExported();

signals:
	void FacesChanged();
	void NameChanged();
	void OptionsChanged(); // format, size or mipmaps
	void ModifiedChanged( bool bModified );

private:
	friend class CSkySlotsCommand;
	void ApplySlots( const std::array<Slot_t, 6> &all );
	void SettingChanged();
	void UpdateModified();

	QString m_szName;
	std::array<Slot_t, 6> m_Slots;
	mutable std::optional<xv::sky::CCube> m_Cube;
	xv::texture::VtfFormat_t m_nFormat = xv::texture::VtfFormat_t::BGR888;
	int m_nSize = 0;
	bool m_bMipmaps = false;
	std::filesystem::path m_ExportFolder;
	std::filesystem::path m_LastExportFolder;
	QString m_szOrigin;
	QUndoStack *m_pUndoStack = nullptr;
	bool m_bSettingsChanged = false; // name or options; they aren't undoable
	bool m_bModified = false;        // as last signalled
};
