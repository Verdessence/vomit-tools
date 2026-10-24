//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Typed views and token edits for detail.vbsp, with schema and resource checks.
//===========================================================================//
#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "kv1/Kv1.h"
#include "material/Material.h"

namespace xv::detail
{
using NodePath_t = std::vector<std::size_t>;
struct Entry_t
{
	NodePath_t m_Path;
	std::string m_szName;
	std::string m_szModel;
	std::optional<std::array<double, 5>> m_Sprite;
	std::optional<std::array<double, 4>> m_Size;
	double m_flAmount = 0;
};
struct Group_t
{
	NodePath_t m_Path;
	std::string m_szName;
	double m_flAlpha = 0;
	std::vector<Entry_t> m_Entries;
};
struct Type_t
{
	NodePath_t m_Path;
	std::string m_szName;
	double m_flDensity = 0;
	std::vector<Group_t> m_Groups;
};
struct KeyDef_t
{
	std::string m_szKey;
	std::string m_szKind;
	std::string m_szLabel;
	std::string m_szDefault;
	std::string m_szUnits;
	int m_nDepth = 0;
};
class CSchema
{
public:
	[[nodiscard]] static CSchema Parse( std::string_view szText );
	[[nodiscard]] const KeyDef_t *Find( std::string_view szKey, int nDepth ) const;
	[[nodiscard]] const auto &GetKeys() const { return m_Keys; }

private:
	std::vector<KeyDef_t> m_Keys;
};
struct Issue_t
{
	NodePath_t m_Path;
	std::string m_szMessage;
};
struct LintContext_t
{
	std::function<bool( std::string_view )> m_pfnExists;
	std::string m_szMaterial = "detail/detailsprites";
	// nullopt means that no complete material scan is available.
	std::optional<std::map<std::string, std::vector<std::string>>> m_Uses;
};
[[nodiscard]] const kv1::CNode *Node( const kv1::CDocument &document, const NodePath_t &path );
[[nodiscard]] kv1::CNode *Node( kv1::CDocument &document, const NodePath_t &path );
[[nodiscard]] std::vector<Type_t> Types( const kv1::CDocument &document );
[[nodiscard]] std::vector<double> Numbers( std::string_view szValue );
bool Set( kv1::CDocument &document, const NodePath_t &path, std::string_view szKey, std::string_view szValue );
[[nodiscard]] std::vector<Issue_t> Lint( const kv1::CDocument &document, const CSchema &schema, const LintContext_t &context = {} );
[[nodiscard]] std::map<std::string, std::vector<std::string>> ScanMaterials( const std::vector<std::string> &files, const material::Reader_t &pfnRead );
} // namespace xv::detail
