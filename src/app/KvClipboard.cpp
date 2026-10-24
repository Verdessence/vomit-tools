//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See KvClipboard.h.
//
//===========================================================================//
#include "KvClipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

namespace
{

// Marks text this app copied, though any KeyValues text pastes.
constexpr auto MIME_TYPE = "application/x-vomit-tools-keyvalues";

//-----------------------------------------------------------------------------
// Purpose: The clipboard's text: ours first, else plain text.
//-----------------------------------------------------------------------------
QByteArray ClipboardText()
{
	const QMimeData *pData = QGuiApplication::clipboard()->mimeData();
	if ( !pData )
	{
		return {};
	}
	if ( pData->hasFormat( QString::fromLatin1( MIME_TYPE ) ) )
	{
		return pData->data( QString::fromLatin1( MIME_TYPE ) );
	}
	return pData->hasText() ? pData->text().toUtf8() : QByteArray{};
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Puts nodes on the clipboard, laid out afresh.
//-----------------------------------------------------------------------------
void CopyNodes( const std::vector<xv::kv1::CNode> &nodes )
{
	xv::kv1::CDocument document;
	for ( const xv::kv1::CNode &node : nodes )
	{
		document.InsertRoot( document.m_Roots.size(), node, xv::kv1::Layout_t::LAYOUT_INFER );
	}
	const QByteArray text = QByteArray::fromStdString( xv::kv1::Write( document ) );
	auto *pData = new QMimeData;
	pData->setData( QString::fromLatin1( MIME_TYPE ), text );
	pData->setText( QString::fromUtf8( text ) );
	QGuiApplication::clipboard()->setMimeData( pData );
}

//-----------------------------------------------------------------------------
// Purpose: The nodes on the clipboard.
//-----------------------------------------------------------------------------
std::vector<xv::kv1::CNode> PastedNodes()
{
	const QByteArray text = ClipboardText();
	if ( text.trimmed().isEmpty() )
	{
		return {};
	}
	const xv::kv1::CParseResult parsed = xv::kv1::Parse( text.toStdString() );
	if ( parsed.HasErrors() )
	{
		return {};
	}
	std::vector<xv::kv1::CNode> nodes;
	for ( const xv::kv1::CNode &node : parsed.m_Document.m_Roots )
	{
		if ( node.IsPair() || node.IsBlock() )
		{
			nodes.push_back( node );
		}
	}
	return nodes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CanPasteNodes()
{
	return !PastedNodes().empty();
}
