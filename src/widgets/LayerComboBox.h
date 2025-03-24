#ifndef HFUT_GUI_LAYERCOMBOBOX_H
#define HFUT_GUI_LAYERCOMBOBOX_H

#include <QComboBox>
#include <QStandardItemModel>
#include <QListView>
//#include <QAbstractItemView>
#include <QKeyEvent>
#include <QVariant> 

class QLineEdit;
class QListView; 

struct ItemInfo
{	
    int idx;
    QString layerName;	
    QVariant userData;
    bool visibleChecked; 

    ItemInfo()	
    {
        idx = -1;
        layerName = QString("");	
        userData = QVariant();	
        visibleChecked = false;
    }
};

//事件过滤器
/**/
class KeyPressEater : public QObject
{
    Q_OBJECT
    public:  
        KeyPressEater(QObject* parent=nullptr) : QObject(parent) {}
        ~KeyPressEater() {}

    signals: 
        void signalActivated(int idx); 

    protected:
        bool eventFilter(QObject *obj, QEvent *event)   
        {
            if (event->type() == QEvent::KeyPress)    
            {
                QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event); 
                if (keyEvent->key() == Qt::Key_Space) 
                {
                    QListView* lstV = qobject_cast<QListView*>(obj);
                    //QAbstractItemView* lstV = qobject_cast<QAbstractItemView*>(obj);
                    if (nullptr != lstV)   
                    {
                        int idx = lstV->currentIndex().row();   
                        if (-1 != idx) 
                        {
                            emit signalActivated(idx);
                        }
                    }
                }
                else if (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down)  
                {
                    return QObject::eventFilter(obj, event);           
                }
                return true;    
            } 
            else { 
                // standard event processing 
                return QObject::eventFilter(obj, event);    
            }
        }
};
/**/

class LayerComboBox : public QComboBox
{
    Q_OBJECT

    public:
        LayerComboBox(QWidget *parent = Q_NULLPTR);
        ~LayerComboBox(); 

        // 添加item	
        void AddItem(const QString &layerName = QString("New Layer"), bool visibleChecked = true, const QVariant &userData = QVariant());	
        void AddItems(const QList<ItemInfo> &lstItemInfo);	
        //void AddItems(const QMap<QString, bool>& mapStrChk);	
        void AddItems(const QList<QString> &lstStr);	

        // 删除item	
        void RemoveItem(int idx);	

        // 清空item
        //void Clear();	
        
        // 获取选中的数据字符串 
        //QStringList GetSelItemsText();
        QString GetSelItemText();

        // 获取选中item的信息
        //QList<ItemInfo> GetSelItemsInfo();
        ItemInfo GetSelItemInfo();

        // 获取选中的item
        QStandardItem* GetSelItem();

        // 获取item文本
        QString GetItemText(int idx);	

        // 获取item信息
        ItemInfo GetItemInfo(int idx);

        // 获取item
        QStandardItem* GetItem(int idx);
        
        // 获取m_model的行数
        int GetNumRows();

signals:
        // popup显示信号
        void showingPopup();	
        // popup隐藏信号
        void hidingPopup();

        // void currentIndex();
        void currentActiveIndex(int index);

    protected:
        void showPopup();	
        // 重写QComboBox的hidePopup函数	
        // 目的选择过程中，不要隐藏listview	
        void hidePopup();
        virtual void mousePressEvent(QMouseEvent * event);
        virtual void mouseReleaseEvent(QMouseEvent * event);	
        virtual void mouseMoveEvent(QMouseEvent * event);

    private:
        void UpdateText();

    private slots:
        void slotLayerIndexChanged(int idx);
    
        void slotActivated(int idx);
        void slotVisible(int idx);

    private:
        QLineEdit* pLineEdit;  
        QListView* pListView;
        //QTableView* pTableView;
        QStandardItemModel *m_model;
        QWidget *parentWindow;
};


#endif //HFUT_GUI_LAYERCOMBOBOX_H
