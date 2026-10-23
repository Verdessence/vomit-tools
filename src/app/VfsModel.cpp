//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See VfsModel.h.
//
//===========================================================================//
#include "VfsModel.h"

#include <algorithm>
#include <unordered_map>

#include <QApplication>
#include <QStyle>

#include "vfs/Vfs.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVfsModel::CVfsModel( QObject *pParent )
	: QAbstractItemModel( pParent ),
	  m_pRoot( std::make_unique<Node_t>() )
{}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVfsModel::~CVfsModel() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CVfsModel::Rebuild( const xv::vfs::CFileSystem &fileSystem )
{
	beginResetModel();
	m_pRoot = std::make_unique<Node_t>();
	m_nFileCount = 0;

	std::unordered_map<std::string, Node_t *> folders{ { "", m_pRoot.get() } };
	for ( const auto &[ path, mount ] : fileSystem.Files( "game" ) )
	{
		Node_t *pParent = m_pRoot.get();
		std::size_t nStart = 0;
		for ( auto nSlash = path.find( '/' ); nSlash != std::string::npos; nSlash = path.find( '/', nStart ) )
		{
			const std::string szFolder = path.substr( 0, nSlash );
			auto [ it, inserted ] = folders.try_emplace( szFolder, nullptr );
			if ( inserted )
			{
				auto pNode = std::make_unique<Node_t>();
				pNode->m_szName = QString::fromStdString( path.substr( nStart, nSlash - nStart ) );
				pNode->m_pParent = pParent;
				it->second = pParent->m_Children.emplace_back( std::move( pNode ) ).get();
			}
			pParent = it->second;
			nStart = nSlash + 1;
		}
		auto pFile = std::make_unique<Node_t>();
		pFile->m_szName = QString::fromStdString( path.substr( nStart ) );
		pFile->m_szPath = path;
		pFile->m_pMount = mount;
		pFile->m_pParent = pParent;
		pParent->m_Children.push_back( std::move( pFile ) );
		m_nFileCount++;
	}

	// Folders first, then by name; then record each node's row.
	const auto arrange = []( auto &self, Node_t &node ) -> void {
		std::ranges::sort( node.m_Children, []( const auto &a, const auto &b ) {
			const bool bAFolder = a->m_szPath.empty();
			const bool bBFolder = b->m_szPath.empty();
			return bAFolder != bBFolder ? bAFolder : a->m_szName < b->m_szName;
		} );
		for ( int nRow = 0; nRow < static_cast<int>( node.m_Children.size() ); nRow++ )
		{
			node.m_Children[ static_cast<std::size_t>( nRow ) ]->m_nRow = nRow;
			self( self, *node.m_Children[ static_cast<std::size_t>( nRow ) ] );
		}
	};
	arrange( arrange, *m_pRoot );
	endResetModel();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QModelIndex CVfsModel::IndexForPath( const std::string &szPath ) const
{
	Node_t *pNode = m_pRoot.get();
	std::size_t nStart = 0;
	while ( nStart <= szPath.size() )
	{
		const auto nSlash = szPath.find( '/', nStart );
		const QString szName = QString::fromStdString( szPath.substr( nStart, nSlash == std::string::npos ? std::string::npos : nSlash - nStart ) );
		const auto it = std::ranges::find_if( pNode->m_Children, [ &szName ]( const auto &child ) { return child->m_szName == szName; } );
		if ( it == pNode->m_Children.end() )
		{
			return {};
		}
		pNode = it->get();
		if ( nSlash == std::string::npos )
		{
			break;
		}
		nStart = nSlash + 1;
	}
	return pNode == m_pRoot.get() ? QModelIndex{} : createIndex( pNode->m_nRow, 0, pNode );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVfsModel::Node_t *CVfsModel::NodeAt( const QModelIndex &index ) const
{
	return index.isValid() ? static_cast<Node_t *>( index.internalPointer() ) : m_pRoot.get();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QModelIndex CVfsModel::index( int nRow, int nColumn, const QModelIndex &parent ) const
{
	const Node_t *pNode = NodeAt( parent );
	if ( nRow < 0 || nColumn < 0 || nColumn >= COLUMN_COLUMN_COUNT || nRow >= static_cast<int>( pNode->m_Children.size() ) )
	{
		return {};
	}
	return createIndex( nRow, nColumn, pNode->m_Children[ static_cast<std::size_t>( nRow ) ].get() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QModelIndex CVfsModel::parent( const QModelIndex &child ) const
{
	const Node_t *pNode = NodeAt( child );
	if ( !child.isValid() || pNode->m_pParent == m_pRoot.get() )
	{
		return {};
	}
	return createIndex( pNode->m_pParent->m_nRow, 0, pNode->m_pParent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CVfsModel::rowCount( const QModelIndex &parent ) const
{
	if ( parent.column() > 0 )
	{
		return 0;
	}
	return static_cast<int>( NodeAt( parent )->m_Children.size() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CVfsModel::columnCount( const QModelIndex & ) const
{
	return COLUMN_COLUMN_COUNT;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QVariant CVfsModel::data( const QModelIndex &index, int nRole ) const
{
	if ( !index.isValid() )
	{
		return {};
	}
	const Node_t *pNode = NodeAt( index );
	const bool bFolder = pNode->m_szPath.empty();
	switch ( nRole )
	{
		case Qt::DisplayRole:
			if ( index.column() == COLUMN_NAME_COLUMN )
			{
				return pNode->m_szName;
			}
			return pNode->m_pMount ? QString::fromStdString( pNode->m_pMount->GetLocation().filename().string() ) : QVariant{};
		case Qt::ToolTipRole:
			return pNode->m_pMount ? QString::fromStdString( pNode->m_pMount->GetLocation().string() ) : QVariant{};
		case Qt::DecorationRole:
			if ( index.column() == COLUMN_NAME_COLUMN )
			{
				return QApplication::style()->standardIcon( bFolder ? QStyle::SP_DirIcon : QStyle::SP_FileIcon );
			}
			return {};
		case ROLE_PATH_ROLE:
			return QString::fromStdString( pNode->m_szPath );
		case ROLE_IS_DIRECTORY_ROLE:
			return bFolder;
		default:
			return {};
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QVariant CVfsModel::headerData( int nSection, Qt::Orientation nOrientation, int nRole ) const
{
	if ( nOrientation != Qt::Horizontal || nRole != Qt::DisplayRole )
	{
		return {};
	}
	return nSection == COLUMN_NAME_COLUMN ? tr( "Name" ) : tr( "Read from" );
}
