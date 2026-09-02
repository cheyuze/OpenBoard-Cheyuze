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




#ifndef UBTOOLBARBUTTONGROUP_H_
#define UBTOOLBARBUTTONGROUP_H_


#include <QtGui>
#include <QWidget>
#include <QToolBar>
#include <QToolButton>
#include <QActionGroup>
#include <QSlider>
#include <QLabel>

class UBToolbarButtonGroup : public QWidget
{
    Q_OBJECT;

    public:
        enum PreviewType
        {
            ActionIconPreview,
            ColorSwatchPreview,
            LineWidthPreview,
            EraserSizePreview
        };

        UBToolbarButtonGroup(QToolBar *toolbar, const QList<QAction*> &actions = QList<QAction*>(), QString objectNameprefix = "");
        virtual ~UBToolbarButtonGroup();

        void setIcon(const QIcon &icon, int index);
        void setColor(const QColor &color, int index);
        int currentIndex() const;
        void setLabel(const QString& label);
        void setPreviewType(PreviewType type);

    protected:
        void paintEvent(QPaintEvent *);

    private:
        QToolButton         *mToolButton; // The first button in the toolbar
        QString              mLabel;
        QList<QAction*>      mActions;
        QList<QToolButton*>  mButtons;
        int                  mCurrentIndex;
        bool                 mDisplayLabel;
        QActionGroup*        mActionGroup;
        PreviewType          mPreviewType;
        QSlider*             mSlider;
        QLabel*              mSliderValueLabel;
        QLabel*              mCaptionLabel;

        void refreshPreviewIcons();
        QIcon colorSwatchIcon(const QColor& color) const;
        QIcon lineWidthIcon(const QColor& color, int index) const;
        QIcon eraserSizeIcon(int index) const;
        bool usesContinuousSlider() const;
        int normalizedSliderValue() const;
        void updateSliderAppearance();
        void updateSliderValueLabel();

    public slots:
        void setCurrentIndex(int index);
        void setSliderValue(int value);
        void colorPaletteChanged();
        void displayText(QVariant display);

    private slots:
        void selected(QAction *action);
        void sliderMoved(int value);

    signals:
        void activated(int index);
        void currentIndexChanged(int index);
};

#endif /* UBTOOLBARBUTTONGROUP_H_ */
