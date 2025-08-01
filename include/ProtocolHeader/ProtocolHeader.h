#ifndef PROTOCOLHEADER_H
#define PROTOCOLHEADER_H

#include <vector>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/conf.h>
#include <zlib.h>

#include "load_config/load_config.h"
#include "proto/message_struct.pb.h"

bool encryptData(const std::vector<uint8_t> &plaintext, std::vector<uint8_t> &ciphertext,uint8_t *iv,ProtocolHeader &protocolheader);
void ProtocolHeader_To_Proto(const ProtocolHeader &protocolheader,const std::vector<uint8_t>& ciphertext,TestMsg& msg);
#endif