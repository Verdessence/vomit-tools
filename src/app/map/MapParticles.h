//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: info_particle_system playing in the map's 3D views: the systems found by name in the
//          game's particle files (those its manifest lists, and the map's own manifest), each
//          entity's simulated from its control points (its origin, then the entities its
//          cpoint keys name) with the particle editor's simulation, and drawn as a mesh of quads
//          facing the camera. func_dustmotes and func_dustcloud play alongside (map/Dust), and
//          the detail props vbsp would scatter are drawn with them (map/DetailProps).
//
//===========================================================================//
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <QObject>

#include "map/DetailProps.h"
#include "map/Dust.h"
#include "map/Raster.h"
#include "map/Scene.h"
#include "particles/Render.h"
#include "particles/Simulation.h"
#include "vfs/Vfs.h"

class CMapParticles : public QObject
{
	Q_OBJECT

public:
	explicit CMapParticles( QObject *pParent = nullptr );
	~CMapParticles() override;

	// Reads the game's particle systems on a thread of its own (the manifest's files, then
	// szMapName's own manifest); Ready follows. Without a game there are none.
	void Start( std::optional<xv::vfs::CGameInfo> game, std::string szMapName );
	void Stop();
	[[nodiscard]] bool IsReady() const { return m_bReady; }
	// The scene's info_particle_systems and dust: those new or changed start over, the others
	// play on.
	void SetScene( std::shared_ptr<const xv::map::CScene> pScene );
	// Every system and dust on by seconds (a system that has finished starts again); false when
	// none plays.
	bool Advance( float flSeconds );
	[[nodiscard]] bool IsPlaying() const { return !m_Playing.empty() || !m_Dust.empty(); }
	[[nodiscard]] std::size_t GetPlayingCount() const { return m_Playing.size(); }
	[[nodiscard]] std::size_t GetDustCount() const { return m_Dust.size(); }
	// Each material's %detailtype (by the map's material index): the detail props are placed
	// again with them.
	void SetDetailTypes( std::shared_ptr<const std::vector<std::string>> pTypes );
	[[nodiscard]] std::size_t GetDetailPropCount() const { return m_pDetailProps ? m_pDetailProps->GetProps().size() : 0; }
	[[nodiscard]] std::size_t GetParticleCount() const;
	// The effect names the scene asks for that the game doesn't have.
	[[nodiscard]] const std::vector<std::string> &GetMissing() const { return m_Missing; }
	// Their materials, one per system's definition drawn, and what keeps their textures alive.
	[[nodiscard]] const std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> &GetMaterials() const { return m_pMaterials; }
	// The particles (and dust), and the detail props, seen from an eye (right and up of unit
	// length), their materials numbered from nFirstMaterial on; with pOnly (sorted object
	// numbers), only those systems' and dust's. Null when there are none.
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> BuildMesh( xv::map::Point_t eye, xv::map::Point_t right, xv::map::Point_t up, std::uint32_t nFirstMaterial, const std::vector<std::uint32_t> *pOnly = nullptr, bool bParticles = true, bool bDetail = true ) const;

signals:
	void Ready();

private:
	struct Found_t
	{
		std::shared_ptr<const xv::particles::CLibrary> m_pLibrary;
		std::size_t m_nDefinition = 0;
	};
	struct Playing_t
	{
		std::uint32_t m_nObject = 0;
		std::string m_szKey; // what it was started from: its effect and control points
		std::unique_ptr<xv::particles::CSimulation> m_pSimulation;
		std::shared_ptr<const xv::particles::CLibrary> m_pLibrary;
	};

	// The sprite (and material) of a library's definition, read the first time it is asked for.
	[[nodiscard]] const xv::particles::Sprite_t *SpriteFor( const xv::particles::CLibrary &library, std::size_t nDefinition );
	struct Sprite_t;
	// A sprite's texture and a material of the map's kind for it.
	void MakeMaterial( Sprite_t &sprite );
	// The dust's sprite (particle/sparkles), read the first time it is drawn.
	const Sprite_t &DustSprite();
	// The detail material's sheet, read the first time it is drawn.
	const Sprite_t &DetailSprite();
	// detail.vbsp read again when the map names another; the props placed again.
	void UpdateDetail();

	std::thread m_Worker;
	std::atomic<bool> m_bStop{ false };
	int m_nGeneration = 0;
	bool m_bReady = false;
	std::shared_ptr<const xv::vfs::CFileSystem> m_pFileSystem; // what sprites are read from
	std::map<std::string, Found_t, xv::particles::CLessNoCase> m_Systems;
	std::shared_ptr<const xv::map::CScene> m_pScene;
	std::vector<Playing_t> m_Playing;
	std::vector<std::string> m_Missing;
	// Sprites by library and definition, with their place in m_pMaterials.
	struct Sprite_t
	{
		std::unique_ptr<xv::particles::Sprite_t> m_pSprite;
		std::shared_ptr<xv::map::RasterTexture_t> m_pTexture;
		std::uint32_t m_nMaterial = 0;
	};
	std::map<std::pair<const xv::particles::CLibrary *, std::size_t>, Sprite_t> m_Sprites;
	struct Dust_t
	{
		std::uint32_t m_nObject = 0;
		std::string m_szKey; // its keys and brushes, as it was started
		std::unique_ptr<xv::map::CDust> m_pDust;
	};
	std::vector<Dust_t> m_Dust;
	std::optional<Sprite_t> m_DustSprite;
	std::shared_ptr<const std::vector<std::string>> m_pDetailTypes;
	std::string m_szDetailFile;     // the detail.vbsp m_pDetailDefinitions came from
	std::string m_szDetailMaterial; // the sheet m_DetailSprite is
	std::shared_ptr<const xv::map::DetailTypes_t> m_pDetailDefinitions;
	std::unique_ptr<xv::map::CDetailProps> m_pDetailProps;
	std::optional<Sprite_t> m_DetailSprite;
	std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> m_pMaterials = std::make_shared<const std::vector<xv::map::RasterMaterial_t>>();
};
