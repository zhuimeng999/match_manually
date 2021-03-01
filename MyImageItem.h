//
// Created by lucius on 2/7/21.
//

#ifndef MATCH_MANUALLY_MYIMAGEITEM_H
#define MATCH_MANUALLY_MYIMAGEITEM_H

#include <QGraphicsPixmapItem>

class MyImageItem : public QGraphicsItem {
public:
  explicit MyImageItem(const QImage &image, const QRectF &area);

  QRectF boundingRect() const override;

protected:
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

  void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

  void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

private:
  QRectF m_area;
  QImage m_image;
  QPixmap m_visibleImage;
};


#endif //MATCH_MANUALLY_MYIMAGEITEM_H
