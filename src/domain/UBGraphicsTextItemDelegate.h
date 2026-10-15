/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */




#ifndef UBGRAPHICSTEXTITEMDELEGATE_H_
#define UBGRAPHICSTEXTITEMDELEGATE_H_

#include "UBGraphicsItemDelegate.h"
#include <QPointer>

class QDoubleSpinBox;
class QGraphicsProxyWidget;
class QToolButton;
class UBGraphicsTextItem;

class UBGraphicsTextItemDelegate : public UBGraphicsItemDelegate
{
    Q_OBJECT
public:
    UBGraphicsTextItemDelegate(UBGraphicsTextItem* item, QObject* parent = nullptr);
    ~UBGraphicsTextItemDelegate() override;
    bool isEditable();
    void scaleTextSize(qreal multiplier);
    void recolor();
    QVariant itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant& value) override;
    void createControls() override;
    void freeControls() override;
    bool controlsExist() const override;
    void positionHandles() override;
    qreal titleBarWidth();
    void openMenu(const QPoint& globalPosition);
    void setInteractionActive(bool active);

public slots:
    void contentsChanged();
    virtual void setEditable(bool editable);
    void remove(bool canUndo = true) override;

protected:
    void decorateMenu(QMenu* menu) override;
    void updateMenuActionState() override;
    bool mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    bool mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    bool mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    bool keyPressEvent(QKeyEvent* event) override;
    bool keyReleaseEvent(QKeyEvent* event) override;

private:
    enum textChangeMode { changeSize, scaleSize };
    UBGraphicsTextItem* delegated();
    void customize(QFontDialog& dialog);
    void changeTextSize(qreal factor, textChangeMode mode);
    void updateFormattingControls();
    void applyFormat(const QTextCharFormat& format);
    void applyAlignment(Qt::Alignment alignment);
    void saveTextCursorFormats();
    QPointer<QGraphicsProxyWidget> mTextToolbar;
    QPointer<QToolButton> mFontControl, mColorControl, mBoldControl, mItalicControl, mUnderlineControl, mAlignControl;
    QPointer<QDoubleSpinBox> mSizeControl;
    QAction* mEditableAction = nullptr;
    bool mInteracting = false;
    const int delta = 5;

private slots:
    void pickFont();
    void pickColor();
    void decreaseSize();
    void increaseSize();
};
#endif
