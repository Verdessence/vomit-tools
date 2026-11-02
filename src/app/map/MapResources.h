//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The textures the 3D views draw a map with, read from the game in the background: a
//          batch of materials at a time, the ones covering most of the map first, so the views
//          fill in while the rest arrive. Also the sky the map names, and the props' models.
//
//===========================================================================//
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <QObject>

#include "map/Lighting.h"
#include "map/Materials.h"
#include "map/Overlay.h"
#include "map/Props.h"
#include "model/Model.h"
#include "map/Raster.h"
#include "map/Scene.h"
#include "map/Sprites.h"
#include "sky/Sky.h"
#include "vfs/Vfs.h"

// Places the material table keeps after the map's own materials, for those edits bring in: they
// are read into them as they come (LoadMaterials). The entity boxes' material comes after them,
// then the sprites', props' and overlays'.
inline constexpr std::uint32_t MAP_MATERIAL_RESERVE = 256;
// The most frames of an animated texture read (AnimatedTexture picks among them).
inline constexpr int MAX_ANIMATED_FRAMES = 64;

class CMapResources : public QObject
{
	Q_OBJECT

public:
	explicit CMapResources( QObject *pParent = nullptr );
	~CMapResources() override;

	// Reads every material of the scene from the game; without a game, nothing is read and
	// materials draw plain. nMaxSize caps a texture's largest side (smaller mips are kept).
	// bDecoded: the textures are also decoded for drawing in software (the card draws from the
	// VTFs' own DXT mips, which take a fraction of the memory).
	void Start( std::shared_ptr<const xv::map::CScene> pScene, std::optional<xv::vfs::CGameInfo> game, int nMaxSize, bool bDecoded );
	[[nodiscard]] bool IsDecoded() const { return m_bDecoded; }
	void Stop();
	// Reads materials of the scene an edit brought in (indices past those it had at Start) into
	// their places; false when they don't fit the places kept for them (Start again then).
	[[nodiscard]] bool LoadMaterials( std::shared_ptr<const xv::map::CScene> pScene, const std::vector<std::uint32_t> &materials );
	// Replaces changed entities' placements in the background, reading new assets as needed.
	// An empty changed list refreshes everything, including overlays on edited receiving faces.
	void Refresh( std::shared_ptr<const xv::map::CScene> pScene, std::vector<std::uint32_t> changed = {} );
	// Where the entity boxes' material is in the table (past the places kept for new ones).
	[[nodiscard]] std::uint32_t GetEntityMaterial() const { return m_nEntityMaterial; }

	// The map's materials as drawn now (one per material of the map), and what keeps their
	// textures alive; both replaced as more arrive.
	[[nodiscard]] std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> GetMaterials() const { return m_pMaterials; }
	[[nodiscard]] std::shared_ptr<const void> GetTextures() const { return m_pTextures; }
	// What the lighting preview needs of the materials read so far, by the same places.
	[[nodiscard]] std::shared_ptr<const std::vector<xv::map::LightMaterial_t>> GetLightMaterials() const { return m_pLightMaterials; }
	[[nodiscard]] std::shared_ptr<const xv::sky::CCube> GetSky() const { return m_pSky; }
	// What reflections of env_cubemap show (ShadeOptions_t::m_pEnvMap); null until read, or when
	// the game has none.
	[[nodiscard]] std::shared_ptr<const xv::sky::CCube> GetEnvMap() const { return m_pEnvMap; }
	// The props placed in the world, once their models and materials are read, and the
	// entities they draw (which then show no box).
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> GetProps() const { return m_pProps; }
	[[nodiscard]] const std::vector<std::uint32_t> &GetPropObjects() const { return m_PropObjects; }
	// The entities shown as sprites, with their materials' sizes.
	[[nodiscard]] std::shared_ptr<const std::vector<xv::map::SpritePlacement_t>> GetSprites() const { return m_pSprites; }
	// The overlays and decals laid on the faces.
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> GetOverlays() const { return m_pOverlays; }
	[[nodiscard]] std::shared_ptr<const xv::map::MeshParts_t> GetPropParts() const { return m_pPropParts; }
	[[nodiscard]] std::shared_ptr<const xv::map::MeshParts_t> GetOverlayParts() const { return m_pOverlayParts; }
	[[nodiscard]] xv::map::CMapMesh BuildEntityPreview( const xv::map::CScene &scene, const std::vector<std::uint32_t> &objects, xv::map::Point_t right, xv::map::Point_t up, const xv::map::CScene *pBase = nullptr ) const;
	[[nodiscard]] int GetModelsLoaded() const { return m_nModels; }
	[[nodiscard]] int GetModelsTotal() const { return m_nModelsTotal; }
	[[nodiscard]] int GetLoaded() const { return m_nLoaded; }
	[[nodiscard]] int GetTotal() const { return m_nTotal; }
	[[nodiscard]] bool IsFinished() const { return m_bFinished; }
	// Materials whose proxies change them over time (AnimatedTexture, TextureScroll, Sine...).
	[[nodiscard]] bool HasAnimated() const { return !m_Animated.empty(); }
	// Those materials as they are at a time (seconds); true when one changed (GetMaterials then
	// gives the new table).
	bool Animate( float flTime );
	[[nodiscard]] bool IsRefreshing() const { return m_bRefreshing || m_pRefreshNext != nullptr; }

signals:
	void Arrived();
	void Finished();

private:
	// One material as read on the worker.
	struct Loaded_t
	{
		std::uint32_t m_nMaterial = 0;
		xv::map::MaterialInfo_t m_Info;
		std::shared_ptr<xv::map::RasterTexture_t> m_pBase;
		std::shared_ptr<xv::map::RasterTexture_t> m_pBase2;
		std::vector<std::shared_ptr<xv::map::RasterTexture_t>> m_Frames; // an animated material's base texture, every frame (the first is m_pBase)
		std::shared_ptr<xv::map::RasterTexture_t> m_pDetail;
		std::vector<std::shared_ptr<xv::map::RasterTexture_t>> m_DetailFrames; // and its $detail's (the first is m_pDetail)
		bool m_bMissing = false;
		bool m_bTool = false;
		bool m_bNodraw = false;
		bool m_bSprite = false; // drawn unlit, tinted by its entity, blended
		bool m_bAdditive = false; // a sprite added rather than blended by its alpha
		bool m_bOverlay = false;  // an overlay's or a decal's: blended by its alpha
	};

	enum class MaterialUse_t
	{
		MATERIAL_USE_FACES,
		MATERIAL_USE_SPRITES,
		MATERIAL_USE_OVERLAYS,
	};

	// What placing sprites and props has read, kept so that edits only read what is new.
	struct Placing_t
	{
		std::map<xv::map::PropKey_t, xv::map::PropModel_t> m_Models;  // by model and skin (no model: not there)
		std::map<std::string, std::uint32_t, std::less<>> m_Places;    // table places past the map's own, by material name ("+" after a sprite's that adds)
		std::vector<std::array<int, 2>> m_Sizes;                       // by table place: the base texture's size
		std::vector<bool> m_Drawable;
		std::vector<bool> m_Additive;
		std::vector<double> m_DecalScales;
		std::map<std::string, std::uint32_t, std::less<>> m_OverlayPlaces;
		xv::map::MeshParts_t m_OverlayParts;
		xv::map::MeshParts_t m_PropParts;
		std::map<std::uint32_t, std::vector<xv::map::SpritePlacement_t>> m_SpritePlacements;
		std::uint32_t m_nNext = 0;                                     // the table's next free place
	};
	using ReadMaterials_t = std::function<void( const std::vector<std::pair<std::uint32_t, std::string>> &, MaterialUse_t )>;
	using Post_t = std::function<void( std::function<void()> )>;

	void Run( std::shared_ptr<const xv::map::CScene> pScene, xv::vfs::CGameInfo game, int nMaxSize, bool bDecoded, int nGeneration );
	void PlaceEntities( const xv::map::CScene &scene, Placing_t &placing, const ReadMaterials_t &readMaterials, const xv::model::Reader_t &readModel, const Post_t &post, const std::vector<std::uint32_t> &changed = {} );
	void PlaceOverlays( const xv::map::CScene &scene, Placing_t &placing, const ReadMaterials_t &readMaterials, const Post_t &post, const std::vector<std::uint32_t> &changed = {} );
	// Reads materials (their index and name) through read, textures decoded on every core.
	[[nodiscard]] static std::vector<Loaded_t> ReadMaterials( const std::vector<std::pair<std::uint32_t, std::string>> &todo, MaterialUse_t nUse, const xv::material::Reader_t &read, int nMaxSize, bool bDecoded, const std::vector<bool> &additive );
	void Take( std::vector<Loaded_t> batch );

	std::thread m_Worker;
	std::vector<std::thread> m_Extras; // LoadMaterials'
	std::optional<xv::vfs::CGameInfo> m_Game;
	int m_nMaxSize = 1024;
	std::uint32_t m_nEntityMaterial = MAP_MATERIAL_RESERVE;
	Placing_t m_Placing;
	bool m_bRefreshing = false;
	std::shared_ptr<const xv::map::CScene> m_pRefreshNext;
	std::vector<std::uint32_t> m_RefreshChanged;
	std::atomic<bool> m_bStop{ false };
	int m_nGeneration = 0;
	std::vector<xv::map::RasterMaterial_t> m_Current;
	std::vector<std::shared_ptr<xv::map::RasterTexture_t>> m_Keep;
	// A material its proxies move: where it is, what moves it, its frames and how it was read.
	struct Animated_t
	{
		std::uint32_t m_nMaterial = 0;
		std::shared_ptr<const xv::material::CProxies> m_pProxies;
		std::vector<std::shared_ptr<xv::map::RasterTexture_t>> m_Frames;
		std::vector<std::shared_ptr<xv::map::RasterTexture_t>> m_DetailFrames;
		xv::map::RasterMaterial_t m_Still;
	};
	std::vector<Animated_t> m_Animated;
	std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> m_pMaterials;
	std::shared_ptr<const void> m_pTextures;
	std::vector<xv::map::LightMaterial_t> m_LightCurrent;
	std::shared_ptr<const std::vector<xv::map::LightMaterial_t>> m_pLightMaterials;
	std::shared_ptr<const xv::sky::CCube> m_pSky;
	std::shared_ptr<const xv::sky::CCube> m_pEnvMap;
	std::shared_ptr<const xv::map::CMapMesh> m_pProps;
	std::vector<std::uint32_t> m_PropObjects;
	std::shared_ptr<const std::vector<xv::map::SpritePlacement_t>> m_pSprites;
	std::shared_ptr<const xv::map::CMapMesh> m_pOverlays;
	std::shared_ptr<const xv::map::MeshParts_t> m_pPropParts;
	std::shared_ptr<const xv::map::MeshParts_t> m_pOverlayParts;
	int m_nModels = 0;
	int m_nModelsTotal = 0;
	int m_nLoaded = 0;
	int m_nTotal = 0;
	bool m_bFinished = false;
	bool m_bDecoded = true;
};

// A texture for drawing from a VTF: mips from the largest within nMaxSize down to the smallest.
// A DXT texture keeps those mips as they are for the card; bDecode also decodes them (any other
// format is decoded regardless, there being nothing smaller to keep).
[[nodiscard]] std::shared_ptr<xv::map::RasterTexture_t> ReadMapTexture( std::span<const std::byte> vtf, int nMaxSize, bool bDecode = true, int nFrame = 0 );
// How many frames a VTF has (1 for a plain texture, 0 when it can't be read).
[[nodiscard]] int MapTextureFrames( std::span<const std::byte> vtf );
