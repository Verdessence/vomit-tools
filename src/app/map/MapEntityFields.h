//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A few keys of one entity as fields, for the side panels that edit the entities
//          setting a map's look (light_environment, shadow_control, env_fog_controller...):
//          each drawn by its FGD type (a colour button for colours, a list for choices), with
//          the FGD's name and help, and each change one step to undo. A field can also be an
//          input the map sends the entity as it starts (a logic_auto's OnMapSpawn output), as
//          env_tonemap_controller is set. With no such entity in the map, Add makes one.
//
//===========================================================================//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QWidget>

class CMapEditor;
class QFormLayout;
class QLabel;
class QPushButton;

struct EntityField_t
{
	std::string m_szKey;   // the entity's key; empty for an input
	std::string m_szInput; // an input sent to the entity as the map starts
	QString m_szLabel;     // empty: the FGD's name for the key
	QString m_szHelp;      // empty: the FGD's description
};

class CMapEntityFields : public QWidget
{
	Q_OBJECT

public:
	CMapEntityFields( CMapEditor &editor, std::vector<std::string> classes, std::vector<EntityField_t> fields, QWidget *pParent = nullptr );

	// Whether the fields have their own Select button (off where the panel has one).
	void SetSelectButton( bool bShown );
	// The entity shown, by its id; 0 for the map's first of the classes.
	void SetEntityId( int nId );
	// The fields filled again from the map.
	void Refresh();
	// The entity shown, by its index in the map's entities.
	[[nodiscard]] std::optional<std::size_t> GetEntity() const { return m_nEntity; }

	// The edits the fields make (and tests): a key's value (empty takes the key out, back to the
	// game's default), an input's parameter at the start, a new entity of the first class.
	void CommitKey( const std::string &szKey, const std::string &szValue );
	void CommitInput( const std::string &szInput, const std::string &szParameter );
	void AddEntity();
	// What a field shows now (a key's value, or an input's parameter), for tests.
	[[nodiscard]] QString GetFieldText( const std::string &szKeyOrInput ) const;

signals:
	// The fields' entity is another one (or none), or a value changed.
	void Changed();

private:
	struct Field_t
	{
		EntityField_t m_Spec;
		std::string m_szType; // the FGD's type, lower case
		QWidget *m_pEditor = nullptr;
		QLabel *m_pLabel = nullptr;
	};
	// The connection sending an input to the entity (its owner and place), if one does.
	struct Sender_t
	{
		std::size_t m_nEntity = 0;
		std::size_t m_nConnection = 0;
	};
	void Build();
	[[nodiscard]] std::optional<std::size_t> FindEntity() const;
	[[nodiscard]] std::optional<Sender_t> FindSender( const std::string &szInput ) const;
	[[nodiscard]] QString ValueOf( const Field_t &field ) const;
	void PickColour( const Field_t &field );

	CMapEditor &m_Editor;
	std::vector<std::string> m_Classes;
	std::vector<Field_t> m_Fields;
	int m_nId = 0;
	std::optional<std::size_t> m_nEntity;
	bool m_bFilling = false;
	bool m_bSelectButton = true;
	QLabel *m_pTitle = nullptr;
	QPushButton *m_pSelect = nullptr;
	QPushButton *m_pAdd = nullptr;
	QWidget *m_pForm = nullptr;
	QFormLayout *m_pFormLayout = nullptr;
};

// A section that folds: a header button over the content, open or folded as the user last left
// it (kept under szName).
[[nodiscard]] QWidget *MakeFoldSection( const QString &szTitle, QWidget *pContent, const QString &szName, bool bOpen );
