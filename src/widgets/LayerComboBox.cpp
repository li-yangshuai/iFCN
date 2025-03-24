#include <QLineEdit>
#include <QMouseEvent>
#include <QDebug> 

#include "LayerComboBox.h"
#include "../MainWindow.h"

LayerComboBox::LayerComboBox(QWidget *parent) : QComboBox(parent)
{
    pLineEdit = new QLineEdit(this); 
    pLineEdit->setReadOnly(true);  
    this->setLineEdit(pLineEdit);
    this->lineEdit()->disconnect(); 

    m_model = new QStandardItemModel(this);
    this->setModel(m_model); 

    KeyPressEater *keyPressEater = new KeyPressEater(this); 
    pListView = new QListView(this); 
    //pListView->setResizeMode(QListView::Adjust); 
    pListView->installEventFilter(keyPressEater);
    this->setView(pListView);

    parentWindow = parent;

    connect(this, SIGNAL(currentIndexChanged(int)), this, SLOT(slotLayerIndexChanged(int))); 
    connect(this, SIGNAL(activated(int)), this, SLOT(slotActivated(int))); 
    connect(keyPressEater, SIGNAL(signalActivated(int)), this, SLOT(slotActivated(int)));
}

LayerComboBox::~LayerComboBox()
{

} 

void LayerComboBox::AddItem(const QString &layerName /*= QString("New Layer")*/, bool visibleChecked /*= true*/, const QVariant &userData /*= QVariant()*/)
{
    QStandardItem* layerItem = new QStandardItem(layerName);
    layerItem->setCheckable(true);  
    layerItem->setCheckState(visibleChecked ? Qt::Checked : Qt::Unchecked);
    layerItem->setData(userData, Qt::UserRole + 1); 
 
    //qDebug() << tr("m_model") << m_model->rowCount();
    m_model->appendRow(layerItem); 
    //qDebug() << tr("m_model") << m_model->rowCount();

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

    if(idx == -1)
    {
        return;
    }

    QStandardItem* item = m_model->item(idx);  
    //qDebug() << tr("d") << item << item->child(0,0) << item->child(0,1);
    //qDebug() << tr("dd") << item->child(0,0) << item->child(0,0)->row() << item->child(0,0)->column();
    //qDebug() << tr("ddd") << item->child(0,1) << item->child(0,1)->row() << item->child(0,1)->column();

    pLineEdit->setText(item->text()); 
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

    pLineEdit->setText(item->text()); 
}

void LayerComboBox::slotActivated(int idx)
{
    QStandardItem* item = m_model->item(idx);  
    if (nullptr == item) return; 

    Qt::CheckState state = (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked; 
    item->setCheckState(state);  
    pLineEdit->setText(item->text()); 

    slotVisible(idx);
    emit currentActiveIndex(idx);
    
}

void LayerComboBox::slotVisible(int idx)
{
    QStandardItem* item = m_model->item(idx);  
    if (nullptr == item) return; 

    bool state = item->checkState() == Qt::Checked;
    for(auto item : static_cast<MainWindow *>(parentWindow)->layers[idx]){
        item->setVisible(state);
    }

    // static_cast<MainWindow *>(parentWindow)->layers[idx]->setVisible(state);
    // static_cast<MainWindow *>(parentWindow)->layers[idx]->setActive(state);
}
