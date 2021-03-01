//
// Created by lucius on 2/7/21.
//

#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QtCore>
#include "MyImageItem.h"

MyImageItem::MyImageItem(const QImage &image, const QRectF &area) : QGraphicsItem(), m_image(image), m_area(area) {
  setFlag(QGraphicsItem::ItemIsMovable, false);
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  m_visibleImage = QPixmap::fromImage(m_image.copy(m_area.toRect()));
}

QRectF MyImageItem::boundingRect() const {
  auto size = (m_area.bottomRight() - m_area.topLeft());
  if (m_image.isNull()) {
    return QRectF(0, 0, 0, 0);
  } else {
    return QRectF(0, 0, size.x(), size.y());
  }
}

void MyImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
  painter->drawImage(QPointF(0, 0), m_image, m_area);
  auto size = (m_area.bottomRight() - m_area.topLeft());
  painter->setPen(QPen(Qt::yellow, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter->drawEllipse(size / 2, 2, 2);
}

void MyImageItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  QGraphicsItem::mousePressEvent(event);
}

void MyImageItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
  QGraphicsItem::mouseMoveEvent(event);
}


