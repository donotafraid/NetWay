#ifndef  DROPAREA_H
#define DROPAREA_H

#include "load_config/Qt_library.h"
#include "Rust_error_deal/error_deal.h"
// #include "PLC/Map_PLCStruct.h"

enum class DropState{
    Normal,
    HoveringValid,
    HoveringInvalid,
    Success,
    Error
};

class DropArea : public QDialog 
{
    Q_OBJECT
    public:
      explicit DropArea(QWidget *parent = nullptr,
                        QTreeWidgetItem *item = nullptr)
          : QDialog(parent), select_item(item) {
        setAcceptDrops(true);
        setMinimumSize(500, 400);
        setWindowTitle("Drop DataBlock File Here");
        setFocusPolicy(Qt::StrongFocus);
        setDropState(DropState::Normal);

        m_fileListWidget = new QListWidget();
        m_fileListWidget->setSelectionMode(QListWidget::MultiSelection);

        // Buttons
        selectButton = new QPushButton("Select .txt Files");
        processButton = new QPushButton("Process Selected Files");

        connect(selectButton, &QPushButton::clicked, this,
                &DropArea::onSelectFilesClicked);
        connect(processButton, &QPushButton::clicked, this,
                &DropArea::onProcessFiles);

        QVBoxLayout *layout = new QVBoxLayout();
        layout->addWidget(m_fileListWidget);

        QHBoxLayout *buttonLayout = new QHBoxLayout();
        buttonLayout->addWidget(selectButton);
        buttonLayout->addWidget(processButton);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->addLayout(layout);
        mainLayout->addLayout(buttonLayout);

        QTimer::singleShot(100, this, [this]() {
          this->activateWindow();
          this->raise();
          this->setFocus();
          std::cout << "Delayed focus check - hasFocus: " << hasFocus()
                    << std::endl;
        });
      };
    ~DropArea(){
        std::cout<<"~DropArea() called\n";
    };

    Result<bool,RichError> validateFiles(QList<QUrl> &&file_list);
    void setDropState(DropState &&state);

    QTreeWidgetItem *select_item;
    QListWidget *m_fileListWidget;
    QPushButton *selectButton ;
    QPushButton *processButton ;
    QStringList m_fileNameList;

  signals:
    void requestFile(QStringList &fileNameList);

  protected:
    //  WHEN USER DRAG AND DROP FILE INTO AREA , CHANGE VISUAL STATE AND MEAN IT CAN PUT IT
    void dragEnterEvent(QDragEnterEvent* event) override;
    //  WHEN USER DRAG AND DROP FILE LEAVE OUT AREA , RESTORE ORIGINAL VISUAL STATE
    // void dragLeaveEvent(QDragLeaveEvent* event) override;
    //  WHEN USER RELEASE FILE INTO AREA , STSTEM START DEAL WITH FILE AND SEND SIGNALS
    // void dropEvent(QDropEvent* event) override;
    //  DRAW A CUSTOM DRAG AND DROP AREA APPEARANCE , INCLUDING ICONS AND TEXT
    // void paintEvent(QPaintEvent* event) override;

    private slots:
    void onSelectFilesClicked();
    void onProcessFiles();

};

#endif