#include <QLineEdit>
#include <QMouseEvent>
#include <QDebug>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QApplication>

#include "LayerComboBox.h"
#include "ui/mainwindow/MainWindow.h"

namespace {
class LayerItemDelegate : public QStyledItemDelegate
{
public:
    explicit LayerItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    static QRect activeButtonRect(const QRect &itemRect)
    {
        const int height = itemRect.height() - 10;
        return QRect(itemRect.right() - 72, itemRect.top() + 5, 62, height);
    }

    static QRect visibleButtonRect(const QRect &itemRect)
    {
        const QRect activeRect = activeButtonRect(itemRect);
        return QRect(activeRect.left() - 72, itemRect.top() + 5, 62, itemRect.height() - 10);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();

        const QRect outerRect = option.rect.adjusted(4, 2, -4, -2);
        const bool selected = option.state & QStyle::State_Selected;
        const bool visible = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        const bool active = index.data(LayerComboBox::ActiveLayerRole).toBool();

        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? QColor("#dbeafe") : QColor("#f8fafc"));
        painter->drawRoundedRect(outerRect, 8, 8);

        const QRect visibleRect = visibleButtonRect(outerRect);
        const QRect activeRect = activeButtonRect(outerRect);
        const QRect textRect = QRect(outerRect.left() + 12, outerRect.top(),
                                     visibleRect.left() - outerRect.left() - 18, outerRect.height());

        painter->setPen(QColor("#111827"));
        QFont textFont = option.font;
        textFont.setBold(active);
        painter->setFont(textFont);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());

        auto drawBadge = [painter](const QRect &rect, const QString &text,
                                   const QColor &fill, const QColor &border, const QColor &textColor) {
            painter->setPen(QPen(border, 1.2));
            painter->setBrush(fill);
            painter->drawRoundedRect(rect, 7, 7);
            painter->setPen(textColor);
            QFont badgeFont = painter->font();
            badgeFont.setBold(true);
            badgeFont.setPointSize(qMax(8, badgeFont.pointSize() - 1));
            painter->setFont(badgeFont);
            painter->drawText(rect, Qt::AlignCenter, text);
        };

        drawBadge(visibleRect,
                  visible ? QStringLiteral("SHOW") : QStringLiteral("HIDE"),
                  visible ? QColor("#16a34a") : QColor("#ffffff"),
                  visible ? QColor("#166534") : QColor("#6b7280"),
                  visible ? QColor("#ffffff") : QColor("#374151"));

        drawBadge(activeRect,
                  active ? QStringLiteral("EDIT") : QStringLiteral("SET"),
                  active ? QColor("#2563eb") : QColor("#ffffff"),
                  active ? QColor("#1d4ed8") : QColor("#6b7280"),
                  active ? QColor("#ffffff") : QColor("#374151"));

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(220, 34);
    }
};
}

LayerComboBox::LayerComboBox(QWidget *parent) : QComboBox(parent)
{
    pLineEdit = new QLineEdit(this); 
    pLineEdit->setReadOnly(true);  
    pLineEdit->setMinimumHeight(28);
    pLineEdit->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    this->setLineEdit(pLineEdit);
    this->lineEdit()->disconnect(); 

    m_model = new QStandardItemModel(this);
    this->setModel(m_model); 

    KeyPressEater *keyPressEater = new KeyPressEater(this); 
    pListView = new QListView(this); 
    pListView->setItemDelegate(new LayerItemDelegate(pListView));
    pListView->setSpacing(4);
    pListView->viewport()->installEventFilter(this);
    pListView->installEventFilter(keyPressEater);
    this->setView(pListView);
    setMinimumSize(178, 30);
    setMinimumContentsLength(15);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    pLineEdit->setStyleSheet("QLineEdit { padding: 3px 8px; background: #ffffff; }");

    parentWindow = parent;

    connect(this, SIGNAL(currentIndexChanged(int)), this, SLOT(slotLayerIndexChanged(int))); 
    connect(keyPressEater, SIGNAL(signalActivated(int)), this, SLOT(slotActivated(int)));
}

LayerComboBox::~LayerComboBox()
{

} 

void LayerComboBox::AddItem(const QString &layerName /*= QString("New Layer")*/, bool visibleChecked /*= true*/, const QVariant &userData /*= QVariant()*/)
{
    QStandardItem* layerItem = new QStandardItem(layerName);
    layerItem->setCheckState(visibleChecked ? Qt::Checked : Qt::Unchecked);
    layerItem->setData(userData, Qt::UserRole + 1); 
    layerItem->setData(false, ActiveLayerRole);
 
    //qDebug() << tr("m_model") << m_model->rowCount();
    m_model->appendRow(layerItem); 
    //qDebug() << tr("m_model") << m_model->rowCount();

    if (m_model->rowCount() == 1) {
        setCurrentIndex(0);
        setActiveLayer(0);
    }

    UpdateText(); 
}

void LayerComboBox::AddItems(const QList<ItemInfo> &lstItemInfo)
{
    for (auto a : lstItemInfo)
    {
        AddItem(a.layerName, a.visibleChecked, a.userData);  
    }
}

/*
void LayerComboBox::AddItems(const QMap<QString, bool> &mapStrChk)
{
    for (auto it = mapStrChk.begin(); it != mapStrChk.end(); ++it) 
    {
        AddItem(it.key(), it.value());
    }
}
*/

void LayerComboBox::AddItems(const QList<QString> &lstStr)
{
    for (auto a : lstStr)   
    {
        AddItem(a, true); 
    }
}

void LayerComboBox::RemoveItem(int idx)
{
    m_model->removeRow(idx);    
    if (m_model->rowCount() > 0) {
        const int nextIndex = qBound(0, currentIndex(), m_model->rowCount() - 1);
        setCurrentIndex(nextIndex);
        setActiveLayer(nextIndex);
    }
    UpdateText();
} 

/*
void LayerComboBox::Clear()
{
    m_model->clear();  
    UpdateText(); 
}
*/

/*
QStringList LayerComboBox::GetSelItemsText()
{
    QStringList lst;  
    QString layerName = pLineEdit->text();  
    if (layerName.isEmpty())  
    {
        return lst; 
    }
    else   
    {
        return pLineEdit->text().split(",");  
    }
}
*/

QString LayerComboBox::GetSelItemText()
{
    return pLineEdit->text();  
}

/*
QList<ItemInfo> LayerComboBox::GetSelItemsInfo()
{
    QList<ItemInfo> lstInfo;   
    for (int i = 0; i < m_model->rowCount(); i++)  
    {
        QStandardItem* item = m_model->item(i);   
        if (item->checkState() == Qt::Unchecked) continue;    

        ItemInfo info;   
        info.idx = i;  
        info.layerName = item->text();  
        info.visibleChecked = true; 
        info.userData = item->data(Qt::UserRole + 1);   

        lstInfo << info;  
    } 
    return lstInfo;
}
*/

ItemInfo LayerComboBox::GetSelItemInfo()
{
    ItemInfo info;   
    int idx = currentIndex();
    if(idx == -1) return info;

    QStandardItem* item = m_model->item(idx);  
    if (nullptr == item) return info; 

    info.idx = idx;
    info.layerName = item->text(); 
    info.visibleChecked = (item->checkState() == Qt::Checked);   
    info.userData = item->data(Qt::UserRole + 1); 

    return info; 
}

QStandardItem* LayerComboBox::GetSelItem()
{
    int idx = currentIndex();
    if(idx == -1) return nullptr;

    //QStandardItem* item = m_model->item(idx);  
    //return item;
    return m_model->item(idx);  
}
    
QString LayerComboBox::GetItemText(int idx)
{
    if (idx < 0 || idx >= m_model->rowCount()) 
    {
        return QString("");    
    }
    return m_model->item(idx)->text();
}

ItemInfo LayerComboBox::GetItemInfo(int idx)
{
    ItemInfo info;   
    if (idx < 0 || idx >= m_model->rowCount())  
    {
        return info;   
    } 

    QStandardItem* item = m_model->item(idx);

    info.idx = idx;
    info.layerName = item->text(); 
    info.visibleChecked = (item->checkState() == Qt::Checked);   
    info.userData = item->data(Qt::UserRole + 1); 

    return info; 
}

QStandardItem* LayerComboBox::GetItem(int idx)
{
    if (idx < 0 || idx >= m_model->rowCount()) 
    {
        return nullptr;
    }
    return m_model->item(idx);  
}

int LayerComboBox::GetNumRows()
{
    return m_model->rowCount();
}

void LayerComboBox::showPopup()
{
    emit showingPopup();  
    QComboBox::showPopup(); 
}

void LayerComboBox::hidePopup()
{
    int width = this->view()->width();
    int height = this->view()->height();   

    int x = QCursor::pos().x() - mapToGlobal(geometry().topLeft()).x() + geometry().x();    
    int y = QCursor::pos().y() - mapToGlobal(geometry().topLeft()).y() + geometry().y(); 

    QRect rectView(0, this->height(), width, height);  

    if (!rectView.contains(x, y))    
    {
        emit hidingPopup();    
        QComboBox::hidePopup();  
    }
}

bool LayerComboBox::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == pListView->viewport() && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        const QModelIndex index = pListView->indexAt(mouseEvent->pos());
        if (!index.isValid()) {
            return QComboBox::eventFilter(obj, event);
        }

        const QRect itemRect = pListView->visualRect(index);
        if (LayerItemDelegate::visibleButtonRect(itemRect).contains(mouseEvent->pos())) {
            QStandardItem *item = m_model->item(index.row());
            if (item != nullptr) {
                const Qt::CheckState nextState =
                    item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
                item->setCheckState(nextState);
                slotVisible(index.row());
                pListView->viewport()->update(itemRect);
            }
            return true;
        }

        if (LayerItemDelegate::activeButtonRect(itemRect).contains(mouseEvent->pos())) {
            setCurrentIndex(index.row());
            setActiveLayer(index.row());
            emit currentActiveIndex(index.row());
            pListView->viewport()->update();
            return true;
        }
    }

    return QComboBox::eventFilter(obj, event);
}

void LayerComboBox::mousePressEvent(QMouseEvent * event)
{
    QComboBox::mousePressEvent(event);
    event->accept();
}

void LayerComboBox::mouseReleaseEvent(QMouseEvent * event)
{
    QComboBox::mouseReleaseEvent(event);
    event->accept(); 
}

void LayerComboBox::mouseMoveEvent(QMouseEvent * event)
{
    QComboBox::mouseMoveEvent(event);
    event->accept();
}

/*
void LayerComboBox::UpdateText()
{
    QStringList lstTxt;  
    for (int i = 0; i < m_model->rowCount(); ++i) 
    {      
        QStandardItem* item = m_model->item(i); 
        if (item->checkState() == Qt::Unchecked) continue; 

        lstTxt << item->text(); 
    }
    pLineEdit->setText(lstTxt.join(",")); 
    pLineEdit->setToolTip(lstTxt.join("\n"));
}
*/

void LayerComboBox::UpdateText()
{
    int idx = currentIndex();

    if(idx == -1 || idx >= m_model->rowCount())
    {
        pLineEdit->clear();
        return;
    }

    QStandardItem* item = m_model->item(idx);  
    //qDebug() << tr("d") << item << item->child(0,0) << item->child(0,1);
    //qDebug() << tr("dd") << item->child(0,0) << item->child(0,0)->row() << item->child(0,0)->column();
    //qDebug() << tr("ddd") << item->child(0,1) << item->child(0,1)->row() << item->child(0,1)->column();

    pLineEdit->setText(item->text()); 
}

void LayerComboBox::setActiveLayer(int idx)
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (QStandardItem *item = m_model->item(row)) {
            item->setData(row == idx, ActiveLayerRole);
        }
    }
    pListView->viewport()->update();
}

/*
void LayerComboBox::slotActivated(int idx)
{
    QStandardItem* item = m_model->item(idx);
    if (nullptr == item) return; 

    Qt::CheckState state = (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked; 
    item->setCheckState(state);  
    UpdateText();
}
*/

void LayerComboBox::slotLayerIndexChanged(int idx)
{
    QStandardItem* item = m_model->item(idx);  
    if (nullptr == item) return; 

    setActiveLayer(idx);
    pLineEdit->setText(item->text()); 
}

void LayerComboBox::slotActivated(int idx)
{
    QStandardItem* item = m_model->item(idx);  
    if (nullptr == item) return; 

    setCurrentIndex(idx);
    setActiveLayer(idx);
    pLineEdit->setText(item->text()); 
    emit currentActiveIndex(idx);
}

void LayerComboBox::slotVisible(int idx)
{
    QStandardItem* item = m_model->item(idx);  
    if (nullptr == item) return; 

    bool state = item->checkState() == Qt::Checked;
    MainWindow *mainWindow = static_cast<MainWindow *>(parentWindow);
    if (idx >= 0 && idx < mainWindow->layers.size()) {
        for (auto layerItem : mainWindow->layers[idx]) {
            layerItem->setVisible(state);
        }
    }
    mainWindow->scene->setFastLayerVisible(idx, state);

    // static_cast<MainWindow *>(parentWindow)->layers[idx]->setVisible(state);
    // static_cast<MainWindow *>(parentWindow)->layers[idx]->setActive(state);
}
