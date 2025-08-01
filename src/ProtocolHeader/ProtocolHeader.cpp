#include "ProtocolHeader/ProtocolHeader.h"

using  namespace boost::uuids;
bool encryptData(const std::vector<uint8_t> &in_slice, std::vector<uint8_t> &out_Ciphertext, uint8_t *iv,ProtocolHeader &header) {
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS | OPENSSL_INIT_ADD_ALL_CIPHERS, NULL); 
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if(!ctx)
    {
        std::cerr<<"EVP_CIPHER_CTX_new failed!"<<std::endl;
        return -1;
    }

    int rc= RAND_bytes(iv,sizeof(iv));
    if (rc!= 1) 
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    //initialize Encryption context
    rc =(EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc() , NULL, header.AES_KEY.data(), iv) ) ;
    if (rc!= 1) 
    {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        std::cerr<<"EVP_EncryptInit_ex failed and error is : "<<err_buf<<std::endl;
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    out_Ciphertext.resize(in_slice.size() + 2*EVP_CIPHER_CTX_block_size(ctx)); 
    int len , cipherLen = 0 ;

    //Encrypt data (include PKCS7 padding)
    rc =(EVP_EncryptUpdate(ctx, out_Ciphertext.data(), &len, in_slice.data(), in_slice.size()) );
    if (rc!= 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    cipherLen = len;

    // 检查写入位置是否越界
    if((out_Ciphertext.data() + cipherLen) >= (out_Ciphertext.data() + out_Ciphertext.size())) {
        std::cerr<<"Current Write position is out of range"<<std::endl;
        return -1;
    }

    rc =(EVP_EncryptFinal_ex(ctx,out_Ciphertext.data() + cipherLen, &len));
    if (rc!= 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    cipherLen += len;
    out_Ciphertext.resize(cipherLen);

    EVP_CIPHER_CTX_free(ctx);
    return 1;
}


void ProtocolHeader_To_Proto(const ProtocolHeader &protocolheader,const std::vector<uint8_t>&ciphertext,TestMsg& msg)
{
    msg.set_file_id(protocolheader.file_id.data(),protocolheader.file_id.size());
    msg.set_magic(protocolheader.magic);
    msg.set_slice_index(protocolheader.slice_index);
    msg.set_total_slices(protocolheader.total_slices);
    msg.set_iv(protocolheader.iv.data(),protocolheader.iv.size());
    msg.set_aes_key(protocolheader.AES_KEY.data(),protocolheader.AES_KEY.size());
    msg.set_ciphertext(ciphertext.data(),ciphertext.size());
    msg.set_plaintext_size(protocolheader.plaintext_size);
}