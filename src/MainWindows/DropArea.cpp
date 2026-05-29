#include "MainWindows/DropArea.h"

Result<bool,RichError> DropArea::validateFiles(QList<QUrl> &&file_list)
{
    if(file_list.isEmpty())
    {
        return Result<bool,RichError> (RichError("file_list is empty !\n"));
    }

    for (const QUrl& file_urls: file_list)
    {
        QString filePath = file_urls.toLocalFile();
        if(filePath.isEmpty())
        {
            return Result<bool,RichError> (RichError("filePath is empty !\n"));
        }

        QFileInfo fileInfo(filePath);
        if(!fileInfo.exists())
        {
            return Result<bool,RichError> (RichError("fileInfo do not exist !\n"));
        }
        if(!fileInfo.isFile())
        {
            return Result<bool,RichError> (RichError("fileInfo do not mean this is file !\n"));
        }

        QString file_suffix (fileInfo.suffix().toLower());

        if(file_suffix != "txt")
        {
            return Result<bool,RichError> (RichError("fileInfo do not mean this is txt file !\n"));
        }
    }
    return Result<bool,RichError> (true);
}

void DropArea::dragEnterEvent(QDragEnterEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        auto isValid = validateFiles(event->mimeData()->urls());
        if(isValid.is_success())
        {
            event->acceptProposedAction();
            setDropState(DropState::HoveringValid);
        }
        else
        {
            event->ignore();
            setDropState(DropState::HoveringInvalid);
        }
    }
    else
    {
        event->ignore();
        std::cout<<"the event is uneffctive \n";
    }
}

void DropArea::setDropState(DropState&& state)
{
    DropState current_State(state);

    switch(current_State)
    {
        case DropState::Normal:
        setStyleSheet(R"(
            DropArea {
                border: 2px dashed #CCCCCC;
                border-radius: 8px;
                background-color: #FFFFFF;
            }
        )");
        break;
        case DropState::HoveringValid:
         setStyleSheet(R"(
            DropArea {
                border: 2px dashed #007ACC;
                border-radius: 8px;
                background-color: #F0F8FF;
            }
        )");
        break;
        case DropState::HoveringInvalid:
         setStyleSheet(R"(
            DropArea {
                border: 2px dashed #D32F2F;
                border-radius: 8px;
                background-color: #FFEBEE;
            }
        )");
        break;
        case DropState::Success:
         setStyleSheet(R"(
            DropArea {
                border: 2px solid #388E3C;
                border-radius: 8px;
                background-color: #F1F8E9;
            }
        )");
        break;
        case DropState::Error:
         setStyleSheet(R"(
            DropArea {
                    border: 2px solid #D32F2F;
                    border-radius: 8px;
                    background-color: #FFEBEE;
                    padding: 20px;
            }
        )");
        break;
    }
}

void DropArea::onSelectFilesClicked()
{
  m_fileNameList.clear();
  QStringList filePaths =
      QFileDialog::getOpenFileNames(this, "Select DataBlock config files",
                                    "/mnt/c/Users/YA/Desktop/WSL2_tmp_folder",
                                    "Text Files (*.txt) ;;All Files (*)");

  m_fileListWidget->clear();
  for (const QString &filePath : filePaths) {
    m_fileListWidget->addItem(filePath);
    }

    if(m_fileListWidget->count() != 0)
    {
        processButton->setEnabled(true);
    }

    m_fileNameList = std::move(filePaths);
}

void DropArea::onProcessFiles()
{
    emit requestFile(m_fileNameList);
    // m_fileListWidget->clear();
    std::cout<<"onProcessFiles call \n"<<std::endl;
}

