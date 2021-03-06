/***************************************************************************
  qgscustomlayerorderwidget.cpp
  --------------------------------------
  Date                 : May 2014
  Copyright            : (C) 2014 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgscustomlayerorderwidget.h"

#include <QCheckBox>
#include <QListView>
#include <QMimeData>
#include <QVBoxLayout>

#include "qgslayertree.h"
#include "qgslayertreemapcanvasbridge.h"

#include "qgsmaplayer.h"
#include "qgsproject.h"




QgsCustomLayerOrderWidget::QgsCustomLayerOrderWidget( QgsLayerTreeMapCanvasBridge *bridge, QWidget *parent )
  : QWidget( parent )
  , mBridge( bridge )
{
  mModel = new CustomLayerOrderModel( bridge, this );

  mView = new QListView( this );
  mView->setDragEnabled( true );
  mView->setAcceptDrops( true );
  mView->setDropIndicatorShown( true );
  mView->setSelectionMode( QAbstractItemView::ExtendedSelection );
  mView->setDefaultDropAction( Qt::MoveAction );

  mView->setModel( mModel );

  mChkOverride = new QCheckBox( tr( "Control rendering order" ) );
  bridgeHasCustomLayerOrderChanged( bridge->hasCustomLayerOrder() );
  connect( mChkOverride, &QAbstractButton::toggled, bridge, &QgsLayerTreeMapCanvasBridge::setHasCustomLayerOrder );

  connect( bridge, &QgsLayerTreeMapCanvasBridge::hasCustomLayerOrderChanged, this, &QgsCustomLayerOrderWidget::bridgeHasCustomLayerOrderChanged );
  connect( bridge, &QgsLayerTreeMapCanvasBridge::customLayerOrderChanged, this, &QgsCustomLayerOrderWidget::bridgeCustomLayerOrderChanged );

  connect( mModel, &QAbstractItemModel::rowsInserted, this, &QgsCustomLayerOrderWidget::modelUpdated );
  connect( mModel, &QAbstractItemModel::rowsRemoved, this, &QgsCustomLayerOrderWidget::modelUpdated );

  connect( bridge->rootGroup(), &QgsLayerTreeNode::visibilityChanged, this, &QgsCustomLayerOrderWidget::nodeVisibilityChanged );

  QVBoxLayout *l = new QVBoxLayout;
  l->setMargin( 0 );
  l->addWidget( mView );
  l->addWidget( mChkOverride );
  setLayout( l );
}

void QgsCustomLayerOrderWidget::bridgeHasCustomLayerOrderChanged( bool state )
{
  mChkOverride->setChecked( state );
  mModel->refreshModel( mBridge->hasCustomLayerOrder() ? mBridge->customLayerOrder() : mBridge->defaultLayerOrder() );
  mView->setEnabled( state );
}

void QgsCustomLayerOrderWidget::bridgeCustomLayerOrderChanged( const QStringList &order )
{
  Q_UNUSED( order );
  mModel->refreshModel( mBridge->hasCustomLayerOrder() ? mBridge->customLayerOrder() : mBridge->defaultLayerOrder() );
}

void QgsCustomLayerOrderWidget::nodeVisibilityChanged( QgsLayerTreeNode *node )
{
  if ( QgsLayerTree::isLayer( node ) )
  {
    mModel->updateLayerVisibility( QgsLayerTree::toLayer( node )->layerId() );
  }
}

void QgsCustomLayerOrderWidget::modelUpdated()
{
  mBridge->setCustomLayerOrder( mModel->order() );
}



///@cond PRIVATE

CustomLayerOrderModel::CustomLayerOrderModel( QgsLayerTreeMapCanvasBridge *bridge, QObject *parent )
  : QAbstractListModel( parent )
  , mBridge( bridge )
{
}

int CustomLayerOrderModel::rowCount( const QModelIndex & ) const
{
  return mOrder.count();
}

QVariant CustomLayerOrderModel::data( const QModelIndex &index, int role ) const
{
  QString id = mOrder.at( index.row() );

  if ( role == Qt::DisplayRole )
  {
    QgsMapLayer *layer = QgsProject::instance()->mapLayer( id );
    if ( layer )
      return layer->name();
  }

  if ( role == Qt::UserRole + 1 )
  {
    QgsMapLayer *layer = QgsProject::instance()->mapLayer( id );
    if ( layer )
      return layer->id();
  }

  if ( role == Qt::CheckStateRole )
  {
    QgsLayerTreeLayer *nodeLayer = mBridge->rootGroup()->findLayer( id );
    if ( nodeLayer )
      return nodeLayer->isVisible();
  }

  return QVariant();
}

bool CustomLayerOrderModel::setData( const QModelIndex &index, const QVariant &value, int role )
{
  if ( role == Qt::CheckStateRole )
  {
    QString id = mOrder.at( index.row() );
    QgsLayerTreeLayer *nodeLayer = mBridge->rootGroup()->findLayer( id );
    if ( nodeLayer )
    {
      nodeLayer->setItemVisibilityChecked( static_cast< Qt::CheckState >( value.toInt() ) == Qt::Checked );
      return true;
    }
  }
  return false;
}

Qt::ItemFlags CustomLayerOrderModel::flags( const QModelIndex &index ) const
{
  if ( !index.isValid() )
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable;
}

Qt::DropActions CustomLayerOrderModel::supportedDropActions() const
{
  return Qt::CopyAction | Qt::MoveAction;
}

QStringList CustomLayerOrderModel::mimeTypes() const
{
  QStringList types;
  types << QStringLiteral( "application/qgis.layerorderdata" );
  return types;
}

QMimeData *CustomLayerOrderModel::mimeData( const QModelIndexList &indexes ) const
{
  QStringList lst;
  Q_FOREACH ( const QModelIndex &index, indexes )
    lst << data( index, Qt::UserRole + 1 ).toString();

  QMimeData *mimeData = new QMimeData();
  mimeData->setData( QStringLiteral( "application/qgis.layerorderdata" ), lst.join( QStringLiteral( "\n" ) ).toUtf8() );
  return mimeData;
}

bool CustomLayerOrderModel::dropMimeData( const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent )
{
  Q_UNUSED( parent );
  Q_UNUSED( column );

  if ( action == Qt::IgnoreAction )
    return true;

  if ( !data->hasFormat( QStringLiteral( "application/qgis.layerorderdata" ) ) )
    return false;

  QByteArray encodedData = data->data( QStringLiteral( "application/qgis.layerorderdata" ) );
  QStringList lst = QString::fromUtf8( encodedData ).split( '\n' );

  if ( row < 0 )
    row = mOrder.count();

  beginInsertRows( QModelIndex(), row, row + lst.count() - 1 );
  for ( int i = 0; i < lst.count(); ++i )
    mOrder.insert( row + i, lst[i] );
  endInsertRows();

  return true;
}

bool CustomLayerOrderModel::removeRows( int row, int count, const QModelIndex &parent )
{
  Q_UNUSED( parent );
  if ( count <= 0 )
    return false;

  beginRemoveRows( QModelIndex(), row, row + count - 1 );
  while ( --count >= 0 )
    mOrder.removeAt( row );
  endRemoveRows();
  return true;
}

void CustomLayerOrderModel::refreshModel( const QStringList &order )
{
  beginResetModel();
  mOrder = order;
  endResetModel();
}

void CustomLayerOrderModel::updateLayerVisibility( const QString &layerId )
{
  int row = mOrder.indexOf( layerId );
  if ( row != -1 )
    emit dataChanged( index( row ), index( row ) );
}



///@endcond
