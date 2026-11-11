#ifndef UBREGIONFILLCONTROLLER_H
#define UBREGIONFILLCONTROLLER_H
#include <QObject>
#include <QPointer>
#include <QPainterPath>
#include <QColor>
#include <QPolygonF>
#include <memory>

class UBBoardController;
class UBGraphicsScene;
class UBRegionFillPreview;
class QToolButton;
class QToolBar;
class QAction;
class QBoxLayout;

class UBRegionFillController : public QObject
{
public:
    enum Mode { Click, Lasso, Polygon };
    explicit UBRegionFillController(UBBoardController* board);
    ~UBRegionFillController() override;
    void addToToolbar(QToolBar* toolbar, QAction* before, QBoxLayout* inlineLayout = nullptr);
    void start(Mode mode);
    void cancel();
    void apply();
    bool hasPreview() const { return !mPath.isEmpty(); }
    bool isActive() const { return mActive; }
    bool isBusy() const { return mBusy; }
protected:
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    void showHint(const QString& text);
    void clearGesture();
    void refreshButton();
    void updatePreview();
    void detectAt(QPointF viewportPosition);
    void pointerPress(QPointF position);
    void pointerMove(QPointF position);
    void pointerRelease(QPointF position);
    void addPolygonPoint(QPointF viewportPosition);
    void rebuildPolygonPreview(QPointF hover, bool includeHover);
    void finishPolygon();
    void removePolygonPoint();
    UBBoardController* mBoard;
    std::weak_ptr<UBGraphicsScene> mScene;
    QPointer<UBRegionFillPreview> mPreview;
    QPointer<QToolButton> mButton;
    QPointer<QAction> mToolAction;
    QPointer<QAction> mPreviousToolAction;
    QPainterPath mPath;
    QPolygonF mPoints;
    QColor mColor;
    QColor mCustomColor{QStringLiteral("#2563eb")};
    bool mFollowPenColor=true;
    Mode mMode=Lasso;
    int mGap=4;
    qreal mOpacity=.25;
    bool mActive=false;
    bool mPressed=false;
    bool mBusy=false;
    bool mPolygonClosing=false;
    quint64 mDetectionSerial=0;
};
#endif
