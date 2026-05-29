#pragma once
#include "PLC/TransformS7AndOPCUA.h"
#include <cstdlib>
#include <cstring>
#include "PLC/OPC_UA.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include "load_config/Qt_library.h"

using OPCUA_DATA_MAP = std::unordered_map<std::string, UA_Variant>;

class OPCUA_Access;
class S7_Access;
class PLC_Device;

// class IGateway {
// public:
//     virtual ~IGateway() = default;
//     virtual Result<bool, RichError> ReadS7DataBlock_FromPLC(S7Object& client_object) = 0;
//     virtual Result<bool, RichError> adjust_BigEndian_to_LittleEndian(const std::string& variablePath) = 0;
//     virtual Result<std::vector<std::string>, RichError> getVariableList() = 0;
// };

class Single_Data_Block : public QAbstractTableModel {
  Q_OBJECT // ✅ 关键：启用元对象系统

      public : Single_Data_Block();
  ~Single_Data_Block();
  Result<bool, RichError> ReadS7DataBlock_FromPLC(S7Object &client_object);
  Result<bool, RichError> ReadOPCUADataBlock_FromPLC(OPCUA_Access *pointer);
  Result<bool, RichError>
  adjust_BigEndian_to_LittleEndian(const std::string &variablePath);
  Result<bool, RichError> SendBuffer_To_PLC_Offset(S7_Access *client_object);
  Result<bool, RichError> SendBuffer_To_PLC_OPCUA(OPCUA_Access *pointer);
  Result<std::vector<std::string>, RichError> getVariableList();

  Result<bool, RichError> resize_data_block_buffer(const bool is_last_success);
  Result<bool, RichError> add_datablock_from_OPCUADataBlockDefinition(
      OPCUADataBlockDefinition &OPCUADataBlockDefinition);
  Result<bool, RichError>
  add_variable_from_S7XMLVariableDefinition(S7XMLVariableDefinition &variable_definition,
                                       int data_block_number,
                                       const std::string &prefix = "");
  Result<bool, RichError> display_variable_list();
  Result<std::string, RichError> return_DataBlockName();

  Result<bool, RichError> isNumber(const std::string &str);
  Result<bool, RichError> isArray(S7ModernDataStruct &m_var);
  Result<bool, RichError>
  Set_NodeID_And_read(OPCUA_Access *UA_pointer,
                      S7ModernDataStruct &single_data_var);

  Result<int, RichError> getVariableCount();

  Single_Data_Block(const Single_Data_Block &other)
      : m_variable_map(other.m_variable_map),
        data_block_name(other.data_block_name),
        m_data_block_buffer(other.m_data_block_buffer),
        whole_data_block_length(other.whole_data_block_length) {
    std::cout << "Single_Data_Block Copy Constructor\n";
  }

  Single_Data_Block &operator=(const Single_Data_Block &other) {
    if (this != &other) {
      m_variable_map = other.m_variable_map;
      data_block_name = other.data_block_name;
      m_data_block_buffer = other.m_data_block_buffer;
      whole_data_block_length = other.whole_data_block_length;
    }
    std::cout << "Single_Data_Block Copy Assignment\n";
    return *this;
  }

  Single_Data_Block(Single_Data_Block &&other) noexcept
      : m_variable_map(std::move(other.m_variable_map)),
        data_block_name(std::move(other.data_block_name)),
        m_data_block_buffer(std::move(other.m_data_block_buffer)),
        whole_data_block_length(other.whole_data_block_length) {
    std::cout << "Single_Data_Block Move Constructor\n";
  }

  Single_Data_Block &operator=(Single_Data_Block &&other) noexcept {
    if (this != &other) {
      m_variable_map = std::move(other.m_variable_map);
      data_block_name = std::move(other.data_block_name);
      m_data_block_buffer = std::move(other.m_data_block_buffer);
      whole_data_block_length = other.whole_data_block_length;
    }
    std::cout << "Single_Data_Block Move Assignment\n";
    return *this;
  }

  std::unordered_map<std::string, S7ModernDataStruct> m_variable_map;
  std::vector<uint8_t> m_data_block_buffer;
  int whole_data_block_length = 0;

private:
  std::string data_block_name;
  std::vector<uint8_t> tmp_buffer;
  std::unordered_set<std::string> m_hasWrite_OPCUA_map;
};

class DataStructeEditor : public QWidget
{
    Q_OBJECT
public:
    explicit DataStructeEditor(QWidget *parent = nullptr,Single_Data_Block* data_block_pointer = nullptr)
    :m_single_data_block(data_block_pointer){
        this->initialize_table();
        connect(m_tableWidget->horizontalHeader(),&QHeaderView::sortIndicatorChanged,this,&DataStructeEditor::update_RowMapping);
    };

    ~DataStructeEditor() {
        delete m_single_data_block;
        std::cout<<"Single_Data_Block Destroyed !\n";
        std::cout<<"DataStructeEditor Destroyed !\n";
    };

    DataStructeEditor& operator=(const DataStructeEditor &other)
    {
        if(this != &other)
        {
          m_single_data_block = other.m_single_data_block;
          not_need_initialize_table = other.not_need_initialize_table;
          m_tableWidget = other.m_tableWidget;
          m_layout_V = other.m_layout_V;
          m_table_data_buffer_bigEndian = other.m_table_data_buffer_bigEndian;
          m_table_data_buffer_littleEndian = other.m_table_data_buffer_littleEndian;
          m_valueWidget_map = other.m_valueWidget_map;
        }
        std::cout<<"DataStructEditor copy function called"<<std::endl;
        return *this;
    }
  
    Result<bool,RichError> initialize_table();
    Result<bool,RichError> update_LittleEndianBuffer_from_table();
    Result<bool,RichError> update_BigEndianBuffer_from_table();
    Result<bool,RichError> write_Table_From_S7_DatabBlockBuffer();
    Result<bool,RichError> write_Table_From_OPCUA_DatabBlockBuffer();
    Result<bool,RichError> read_DataBlock_from_PLC(PLC_Device *device_pointer);
    Result<bool,RichError> SendBuffer_ToPLC(PLC_Device *device_pointer);
    Result<std::string,RichError> return_DataBlockName();
    void adjust_RowHeight();
    void update_RowMapping();
    QWidget* find_RowMapping(const std::string& var_name, int column);
    Single_Data_Block* m_single_data_block;
private:
    bool not_need_initialize_table = false;
    QTableWidget* m_tableWidget;
    QVBoxLayout* m_layout_V;
    std::vector<uint8_t> m_table_data_buffer_bigEndian;
    std::vector<uint8_t> m_table_data_buffer_littleEndian;

    std::unordered_map<std::string,int> m_valueWidget_map;
protected:
    void resizeEvent(QResizeEvent *event) override;

};

class PLC_Device {
public:
    PLC_Device(S7_Access *S7_pointer,OPCUA_Access *OPCUA_pointer,std::string &ip_Address):m_S7_Access(S7_pointer),m_UA_Access(OPCUA_pointer),m_ip_Address(ip_Address){}
    ~PLC_Device();

    class builder{
        private:
        S7_Access *m_S7_Access = nullptr;
        OPCUA_Access *UA_pointer = nullptr;
        std::string m_ip_Address;

      public:
        builder() = default;
        ~builder() = default;

        builder& set_S7_Access(std::string &ip_Address,int rack,int slot) 
        {
            m_ip_Address = ip_Address;
            m_S7_Access = new S7_Access(ip_Address,rack,slot); 
            return *this;
        }

        builder &
        set_UA_Access(std::string &ip_Address,int nameSpace,int port) {
          m_ip_Address = ip_Address;
          UA_pointer = new OPCUA_Access(UA_Client_new(), ip_Address,nameSpace,port);
          return *this;
        }

        static builder create_builder()
        {
            return builder();
        }

        std::unique_ptr<PLC_Device> unique_ptr_build()
        {
            return std::make_unique<PLC_Device>(m_S7_Access,UA_pointer,m_ip_Address);
        }

        PLC_Device* raw_ptr_build()
        {
            return new PLC_Device(m_S7_Access,UA_pointer,m_ip_Address);
        }
    };
    
    Result<bool,RichError> connect();
    void disconnect();

    // Result<bool,RichError> add_datablock(Single_Data_Block&& data_block); 
    Result<bool,RichError> parse_dataBlock_To_device(std::string &source_file_content);
    Result<bool,RichError> adjust_BigEndian_to_LittleEndian(const std::string& variablePath);
    Result<std::string,RichError> return_input_ipAddress();
    Result<bool,RichError> read_DataBlock(DataStructeEditor *data_struct_editor,PLC_Device *device_pointer);
    Result<bool,RichError> SendBuffer_ToPLC(DataStructeEditor *data_struct_editor,PLC_Device *device_pointer);
    Result<bool,RichError> is_configFile_existInMap(const QString &file_path);
    Result<DataStructeEditor *,RichError> find_related_dataStruct(const std::string &file_path);
    const std::unordered_map<std::string, DataStructeEditor*>& getEditors() const;
    bool parse_file_content( std::string &source_file_content, const std::string &source_file_path);
    
    void add_subTreeWidgetItem(const QString &file_path, QTreeWidgetItem *item);
    void delete_subTreeWidgetItem(const QString &file_path);
    void delete_subConfig(const QString &file_path);

    std::list<QString>  m_dataBlock_config_list;
    
    S7_Access *m_S7_Access;
    OPCUA_Access *m_UA_Access;
    bool is_exist_data_block = false;
    private:
    std::unordered_map<std::string, QTreeWidgetItem*> m_subTreeWidget_map;
    std::unordered_map<std::string,DataStructeEditor *> m_dataStructEditor_map;
    SCL_Parser m_scl_parser;
    std::string m_ip_Address = "";
};

class OPC_UA_client
{
    public:
    OPC_UA_client(UA_Client* client_pointer):m_UA_client(client_pointer){};
    ~OPC_UA_client() = default;

    Result<bool,RichError> extractVariable(const std::string &config_file_path);

    class builder{
        private:
        UA_Client* m_UA_client;

        public:
        builder() = default;
        ~builder() = default;

        builder& set_UA_pointer(UA_Client* device_pointer)
        {
            m_UA_client = device_pointer;
            return *this;
        }

        static builder create_builder()
        {
            return builder();
        }

        std::unique_ptr<OPC_UA_client> unique_ptr_build()
        {
            return std::make_unique<OPC_UA_client>(m_UA_client);
        }
    };
    
    private:
    UA_Client* m_UA_client;
    UA_StatusCode m_UA_client_status;
    std::vector<UA_Variable_Info> m_UA_info_vector;
};

class MultiPLCGateway {
public:
    MultiPLCGateway()=default;
private:
    std::unordered_map<std::string, PLC_Device> m_plc_device_map;
    Result<bool,RichError> add_device_from_provided(OPCUADataBlockDefinition& data_block_definition,const std::string& plc_name);
};

////////////////////////////////////////////////////////////////////
