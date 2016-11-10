// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*!
 * @file AESGCMGMAC_Transform.cpp
 */

#include "AESGCMGMAC_Transform.h"

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
using namespace eprosima::fastrtps::rtps::security;

AESGCMGMAC_Transform::AESGCMGMAC_Transform(){}
AESGCMGMAC_Transform::~AESGCMGMAC_Transform(){}

bool AESGCMGMAC_Transform::encode_serialized_payload(
                std::vector<uint8_t> &encoded_buffer,
                std::vector<uint8_t> &extra_inline_qos,
                const std::vector<uint8_t> &plain_buffer,
                DatawriterCryptoHandle &sending_datawriter_crypto,
                SecurityException &exception){

    AESGCMGMAC_WriterCryptoHandle& local_writer = AESGCMGMAC_WriterCryptoHandle::narrow(sending_datawriter_crypto);
    if(local_writer.nil()){
        //TODO (santi) Provide insight
        return false;
    }
    CipherData *m_cipherdata = nullptr;

    //Step 1 - Find the CriptoData associated to the Writer (or create if this is the first time it is used)
    {
        std::map<CryptoTransformKeyId,CipherData>::iterator it= status.find(local_writer->WriterKeyMaterial.sender_key_id);
        if(it == status.end()){
            //First time the Participant sends data - Init struct
            CipherData new_data;
            new_data.master_key_id = local_writer->WriterKeyMaterial.sender_key_id;
            RAND_bytes( (unsigned char *)(&(new_data.session_id)), sizeof(uint16_t));
            
            new_data.max_blocks_per_session= 12000;//TODO (Santi) - This ough to be configurable
            new_data.session_block_counter= new_data.max_blocks_per_session; //Set to maximum so computation of new SessionKey is triggered
            status[local_writer->WriterKeyMaterial.sender_key_id] = new_data;
            it= status.find(local_writer->WriterKeyMaterial.sender_key_id);
            m_cipherdata = &(it->second);
        }else{
            //Found!
            m_cipherdata = &(it->second);
        }
    }

    //Step 2 - If the maximum number of blocks have been processed, generate a new SessionKey
    if(m_cipherdata->session_block_counter >= m_cipherdata->max_blocks_per_session){
        m_cipherdata->session_id += 1; 

        m_cipherdata->SessionKey = compute_sessionkey(local_writer->WriterKeyMaterial.master_sender_key,
                local_writer->WriterKeyMaterial.master_salt,
                m_cipherdata->session_id);
        
        //ReceiverSpecific keys shall be computed specifically when needed
        m_cipherdata->session_block_counter = 0;
    }

    //Step 2.5 - Build remaining NONCE elements
    uint64_t initialization_vector_suffix;  //iv suffix changes with every operation
    RAND_bytes( (unsigned char*)(&initialization_vector_suffix), sizeof(uint64_t) );
    std::array<uint8_t,12> initialization_vector; //96 bytes, session_id + suffix
    memcpy(initialization_vector.data(),&(m_cipherdata->session_id),4);
    memcpy(initialization_vector.data()+4,&initialization_vector_suffix,8);
    
    //Step 3 - Build SecureDataHeader
    SecureDataHeader header;
    
    header.transform_identifier.transformation_kind = local_writer->WriterKeyMaterial.transformation_kind;
    header.transform_identifier.transformation_key_id = local_writer->WriterKeyMaterial.sender_key_id;
    memcpy( header.session_id.data(), &(m_cipherdata->session_id), 4);
    memcpy( header.initialization_vector_suffix.data() , &initialization_vector_suffix, 8);


    //Step 4 -Cypher the plain rtps message -> SecureDataBody
    OpenSSL_add_all_ciphers();
    int rv = RAND_load_file("/dev/urandom", 32); //Init random number gen

    size_t enc_length = plain_buffer.size()*3;
    std::vector<uint8_t> output;
    output.resize(enc_length,0);

    unsigned char tag[AES_BLOCK_SIZE]; //Container for the Authentication Tag (will become common mac)
    
    int actual_size=0, final_size=0;
    EVP_CIPHER_CTX* e_ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit(e_ctx, EVP_aes_128_gcm(), (const unsigned char*)(m_cipherdata->SessionKey.data()), initialization_vector.data());
    EVP_EncryptUpdate(e_ctx, output.data(), &actual_size, (const unsigned char*)plain_buffer.data(), plain_buffer.size());
    EVP_EncryptFinal(e_ctx, output.data() + actual_size, &final_size);
    EVP_CIPHER_CTX_ctrl(e_ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    output.resize(actual_size+final_size);
    EVP_CIPHER_CTX_free(e_ctx);

    //Step 4.5 - Copy the results into SecureDataBody
    SecureDataBody body;
    body.secure_data.resize(output.size());
    memcpy(body.secure_data.data(),output.data(),output.size());

    //Step 5 - Build Secure DataTag
    SecureDataTag dataTag;
    memcpy(dataTag.common_mac.data(),tag, 16);
    
    //Step 6 - Assemble the message
    encoded_buffer.clear();
    
    //Header
    for(int i=0;i < 4; i++) encoded_buffer.push_back( header.transform_identifier.transformation_kind.at(i) );
    for(int i=0;i < 4; i++) encoded_buffer.push_back( header.transform_identifier.transformation_key_id.at(i) );
    for(int i=0;i < 4; i++) encoded_buffer.push_back( header.session_id.at(i) );
    for(int i=0;i < 8; i++) encoded_buffer.push_back( header.initialization_vector_suffix.at(i) );
    
    //Body
    long body_length = body.secure_data.size();
    for(int i=0;i < sizeof(long); i++) encoded_buffer.push_back( *( (uint8_t*)&body_length + i) );
    for(int i=0;i < body_length;i++) encoded_buffer.push_back( body.secure_data.at(i) );

    //Tag
        //Common tag
    for(int i=0;i < 16; i++) encoded_buffer.push_back( dataTag.common_mac.at(i) );
    return true;
}
         
bool AESGCMGMAC_Transform::encode_datawriter_submessage(
                std::vector<uint8_t> &encoded_rtps_submessage,
                const std::vector<uint8_t> &plain_rtps_submessage,
                const DatawriterCryptoHandle &sending_datawriter_crypto,
                const std::vector<DatareaderCryptoHandle> receiving_datareader_crypto_list,
                SecurityException &exception){

    exception = SecurityException("Not implemented");
    return false;
}
    
bool AESGCMGMAC_Transform::encode_datareader_submessage(
                std::vector<uint8_t> &encoded_rtps_submessage,
                const std::vector<uint8_t> &plain_rtps_submessage,
                const DatareaderCryptoHandle &sending_datareader_crypto,
                const std::vector<DatawriterCryptoHandle> &receiving_datawriter_crypto_list,
                SecurityException &exception){

    exception = SecurityException("Not implemented");
    return false;
}
    
bool AESGCMGMAC_Transform::encode_rtps_message(
                std::vector<uint8_t> &encoded_rtps_message,
                const std::vector<uint8_t> &plain_rtps_message,
                ParticipantCryptoHandle &sending_crypto,
                const std::vector<ParticipantCryptoHandle*> &receiving_crypto_list,
                SecurityException &exception){

    AESGCMGMAC_ParticipantCryptoHandle& local_participant = AESGCMGMAC_ParticipantCryptoHandle::narrow(sending_crypto);
    if(local_participant.nil()){
        //TODO (santi) Provide insight
        return false;
    }
    CipherData *m_cipherdata = nullptr;

    //Step 1 - Find the CriptoData associated to the Participant (or create if this is the first time it is used)
    {
        std::map<CryptoTransformKeyId,CipherData>::iterator it= status.find(local_participant->ParticipantKeyMaterial.sender_key_id);
        if(it == status.end()){
            //First time the Participant sends data - Init struct
            CipherData new_data;
            new_data.master_key_id = local_participant->ParticipantKeyMaterial.sender_key_id;
            RAND_bytes( (unsigned char *)(&(new_data.session_id)), sizeof(uint16_t));
            
            new_data.max_blocks_per_session= 12000;//TODO (Santi) - This ough to be configurable
            new_data.session_block_counter= new_data.max_blocks_per_session; //Set to maximum so computation of new SessionKey is triggered
            status[local_participant->ParticipantKeyMaterial.sender_key_id] = new_data;
            it= status.find(local_participant->ParticipantKeyMaterial.sender_key_id);
            m_cipherdata = &(it->second);
        }else{
            //Found!
            m_cipherdata = &(it->second);
        }
    }

    //Step 2 - If the maximum number of blocks have been processed, generate a new SessionKey
    if(m_cipherdata->session_block_counter >= m_cipherdata->max_blocks_per_session){
        m_cipherdata->session_id += 1; 

        m_cipherdata->SessionKey = compute_sessionkey(local_participant->ParticipantKeyMaterial.master_sender_key,
                local_participant->ParticipantKeyMaterial.master_salt,
                m_cipherdata->session_id);
        
        //ReceiverSpecific keys shall be computed specifically when needed
        m_cipherdata->session_block_counter = 0;
    }

    //Step 2.5 - Build remaining NONCE elements
    uint64_t initialization_vector_suffix;  //iv suffix changes with every operation
    RAND_bytes( (unsigned char*)(&initialization_vector_suffix), sizeof(uint64_t) );
    std::array<uint8_t,12> initialization_vector; //96 bytes, session_id + suffix
    memcpy(initialization_vector.data(),&(m_cipherdata->session_id),4);
    memcpy(initialization_vector.data()+4,&initialization_vector_suffix,8);
    
    //Step 3 - Build SecureDataHeader
    SecureDataHeader header;
    
    header.transform_identifier.transformation_kind = local_participant->ParticipantKeyMaterial.transformation_kind;
    header.transform_identifier.transformation_key_id = local_participant->ParticipantKeyMaterial.sender_key_id;
    memcpy( header.session_id.data(), &(m_cipherdata->session_id), 4);
    memcpy( header.initialization_vector_suffix.data() , &initialization_vector_suffix, 8);


    //Step 4 -Cypher the plain rtps message -> SecureDataBody
    OpenSSL_add_all_ciphers();
    int rv = RAND_load_file("/dev/urandom", 32); //Init random number gen

    size_t enc_length = plain_rtps_message.size()*3;
    std::vector<uint8_t> output;
    output.resize(enc_length,0);

    unsigned char tag[AES_BLOCK_SIZE]; //Container for the Authentication Tag (will become common mac)
    
    int actual_size=0, final_size=0;
    EVP_CIPHER_CTX* e_ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit(e_ctx, EVP_aes_128_gcm(), (const unsigned char*)(m_cipherdata->SessionKey.data()), initialization_vector.data());
    EVP_EncryptUpdate(e_ctx, output.data(), &actual_size, (const unsigned char*)plain_rtps_message.data(), plain_rtps_message.size());
    EVP_EncryptFinal(e_ctx, output.data() + actual_size, &final_size);
    EVP_CIPHER_CTX_ctrl(e_ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    output.resize(actual_size+final_size);
    EVP_CIPHER_CTX_free(e_ctx);

    //Step 4.5 - Copy the results into SecureDataBody
    SecureDataBody body;
    body.secure_data.resize(output.size());
    memcpy(body.secure_data.data(),output.data(),output.size());

    //Step 5 - Build Secure DataTag
    SecureDataTag dataTag;
    memcpy(dataTag.common_mac.data(),tag, 16);
    
    //Check the list of receivers, search for keys and compute session keys as needed
    for(auto rec = receiving_crypto_list.begin(); rec != receiving_crypto_list.end(); ++rec){

        AESGCMGMAC_ParticipantCryptoHandle& remote_participant = AESGCMGMAC_ParticipantCryptoHandle::narrow(**rec);
        if(remote_participant.nil()){
            //TODO (santi) Provide insight
            return false;
        }

        //Find or compute ReceiverSpecificKey into m_cipherdata_r   
        CipherData *m_cipherdata_r = nullptr;
 
        {
            std::map<CryptoTransformKeyId,CipherData>::iterator it= status.find(remote_participant->Participant2ParticipantKeyMaterial.at(0).receiver_specific_key_id);
            if(it == status.end()){
                //First time the Participant sends data - Init struct
                CipherData new_data;
                new_data.master_key_id = remote_participant->Participant2ParticipantKeyMaterial.at(0).receiver_specific_key_id; 
                new_data.session_id = m_cipherdata->session_id + 2; //Just make it different in order to trigger Key update
                RAND_bytes( (unsigned char *)(&(new_data.session_id)), sizeof(uint16_t));
            
                status[remote_participant->Participant2ParticipantKeyMaterial.at(0).receiver_specific_key_id] = new_data;
                it= status.find(remote_participant->Participant2ParticipantKeyMaterial.at(0).receiver_specific_key_id);
                m_cipherdata_r = &(it->second);
            }else{
                //Found!
                m_cipherdata_r = &(it->second);
            }
            //Update the key if needed
            if(m_cipherdata_r->session_id != m_cipherdata->session_id){
                //Update triggered!
                m_cipherdata_r->SessionKey = compute_sessionkey(remote_participant->Participant2ParticipantKeyMaterial.at(0).master_receiver_specific_key,
                    remote_participant->Participant2ParticipantKeyMaterial.at(0).master_salt,
                    m_cipherdata->session_id);
                m_cipherdata_r->session_id = m_cipherdata->session_id;
            }
        }   

        //Obtain MAC using ReceiverSpecificKey and the same Initialization Vector as before
        int actual_size=0, final_size=0;
        EVP_CIPHER_CTX* e_ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit(e_ctx, EVP_aes_128_gcm(), (const unsigned char*)(m_cipherdata_r->SessionKey.data()), initialization_vector.data());
        EVP_EncryptUpdate(e_ctx, NULL, &actual_size, dataTag.common_mac.data(), 16);
        EVP_EncryptFinal(e_ctx, output.data() + actual_size, &final_size);
        EVP_CIPHER_CTX_ctrl(e_ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
        output.resize(actual_size+final_size);
        EVP_CIPHER_CTX_free(e_ctx);
        
        ReceiverSpecificMAC buffer;
        buffer.receiver_mac_key_id = m_cipherdata_r->master_key_id;
        memcpy(buffer.receiver_mac.data(),tag,16);
        //Push the MAC into the dataTag
        dataTag.receiver_specific_macs.push_back(buffer);
    }

    //Step 6 - Assemble the message
    encoded_rtps_message.clear();
    
    //Header
    for(int i=0;i < 4; i++) encoded_rtps_message.push_back( header.transform_identifier.transformation_kind.at(i) );
    for(int i=0;i < 4; i++) encoded_rtps_message.push_back( header.transform_identifier.transformation_key_id.at(i) );
    for(int i=0;i < 4; i++) encoded_rtps_message.push_back( header.session_id.at(i) );
    for(int i=0;i < 8; i++) encoded_rtps_message.push_back( header.initialization_vector_suffix.at(i) );
    
    //Body
    long body_length = body.secure_data.size();
    for(int i=0;i < sizeof(long); i++) encoded_rtps_message.push_back( *( (uint8_t*)&body_length + i) );
    for(int i=0;i < body_length;i++) encoded_rtps_message.push_back( body.secure_data.at(i) );

    //Tag
        //Common tag
    for(int i=0;i < 16; i++) encoded_rtps_message.push_back( dataTag.common_mac.at(i) );
        //Receiver specific macs
    long specific_length = dataTag.receiver_specific_macs.size();
    for(int i=0;i < sizeof(long); i++) encoded_rtps_message.push_back( *( (uint8_t*)&specific_length + i ) );
    for(int j=0; j< dataTag.receiver_specific_macs.size(); j++){
        for(int i=0;i < 4; i++) encoded_rtps_message.push_back( dataTag.receiver_specific_macs.at(j).receiver_mac_key_id.at(i) );
        for(int i=0;i < 16; i++) encoded_rtps_message.push_back( dataTag.receiver_specific_macs.at(j).receiver_mac.at(i) );
    }
    return true;
}

bool AESGCMGMAC_Transform::decode_rtps_message(
                std::vector<uint8_t> &plain_buffer,
                const std::vector<uint8_t> &encoded_buffer,
                const ParticipantCryptoHandle &receiving_crypto,
                ParticipantCryptoHandle &sending_crypto,
                SecurityException &exception){

    AESGCMGMAC_ParticipantCryptoHandle& sending_participant = AESGCMGMAC_ParticipantCryptoHandle::narrow(sending_crypto);

    //Fun reverse order process;
    SecureDataHeader header;
    SecureDataBody body;
    SecureDataTag tag;

    //Header
    for(int i=0;i<4;i++) header.transform_identifier.transformation_kind.at(i) = ( encoded_buffer.at( i ) );
    for(int i=0;i<4;i++) header.transform_identifier.transformation_key_id.at(i) = ( encoded_buffer.at( i+4 ) );
    for(int i=0;i<4;i++) header.session_id.at(i) = ( encoded_buffer.at( i+8 ) );
    for(int i=0;i<8;i++) header.initialization_vector_suffix.at(i) = ( encoded_buffer.at( i+12 ) );
    //Body
    long body_length = 0;
    memcpy(&body_length, encoded_buffer.data()+20, sizeof(long));
    for(int i=0;i < body_length; i++) body.secure_data.push_back( encoded_buffer.at( i+20+sizeof(long) ) );
    //Tag
        //common_mac
    for(int i=0;i < 16; i++) tag.common_mac.at(i) = ( encoded_buffer.at( i+20+sizeof(long)+body_length ) );
        //receiver_specific_mac 
    long spec_length = 0;
    memcpy(&spec_length, encoded_buffer.data()+36+sizeof(long)+body_length, sizeof(long));
    //Read specific MACs in search for the correct one (verify the authenticity of the message)
    ReceiverSpecificMAC specific_mac;
    bool mac_found = false; 
    for(int j=0; j < spec_length; j++){
        memcpy( &(specific_mac.receiver_mac_key_id),
                encoded_buffer.data() + 36 + sizeof(long) + body_length + sizeof(long) + j*(20),
                4 );
        memcpy( specific_mac.receiver_mac.data(),
                encoded_buffer.data() + 36 + sizeof(long) + body_length+sizeof(long) + j*(20) + 4,
                16 );
        //Check if it matches the key we have
        if(sending_participant->RemoteParticipant2ParticipantKeyMaterial.at(0).receiver_specific_key_id == specific_mac.receiver_mac_key_id){
            mac_found = true;
            break;
        }
    }

    if(!mac_found){
        exception = SecurityException("Message does not contain a suitable specific MAC for the receiving Participant");
        return false;
    }
    uint32_t session_id;
    memcpy(&session_id,header.session_id.data(),4);
    //Sessionkey
    std::array<uint8_t,32> session_key = compute_sessionkey(
            sending_participant->RemoteParticipant2ParticipantKeyMaterial.at(0).master_sender_key,
            sending_participant->RemoteParticipant2ParticipantKeyMaterial.at(0).master_salt,
            session_id);
    //IV
    std::array<uint8_t,12> initialization_vector;
    memcpy(initialization_vector.data(), header.session_id.data(), 4);
    memcpy(initialization_vector.data() + 4, header.initialization_vector_suffix.data(), 8);

    //Auth message - The point is that we cannot verify the authorship of the message with our receiver_specific_key the message could be crafted
    bool auth = false; 
    
    OpenSSL_add_all_ciphers();
    RAND_load_file("/dev/urandom",32);

    EVP_CIPHER_CTX *d_ctx = EVP_CIPHER_CTX_new();
    plain_buffer.clear();
    plain_buffer.resize(encoded_buffer.size());

    int actual_size = 0, final_size = 0;
  
    //Get ReceiverSpecificSessionKey
    std::array<uint8_t,32> specific_session_key = compute_sessionkey(
                    sending_participant->RemoteParticipant2ParticipantKeyMaterial.at(0).master_receiver_specific_key,
                    sending_participant->RemoteParticipant2ParticipantKeyMaterial.at(0).master_salt,
                    session_id);
    
    //Verify specific MAC
    EVP_DecryptInit(d_ctx, EVP_aes_128_gcm(), (const unsigned char *)specific_session_key.data(), initialization_vector.data());
    EVP_DecryptUpdate(d_ctx, NULL, &actual_size, tag.common_mac.data(), 16);
    EVP_CIPHER_CTX_ctrl( d_ctx, EVP_CTRL_GCM_SET_TAG,16, specific_mac.receiver_mac.data() );
    auth = EVP_DecryptFinal_ex(d_ctx, plain_buffer.data() + actual_size, &final_size); 
    EVP_CIPHER_CTX_free(d_ctx);
    plain_buffer.resize(actual_size + final_size);

    if(!auth){
        //Log error
        return false;
    }
    
    //Decode message
    OpenSSL_add_all_ciphers();
    RAND_load_file("/dev/urandom",32);

    d_ctx = EVP_CIPHER_CTX_new();
    plain_buffer.clear();
    plain_buffer.resize(encoded_buffer.size());

    actual_size = 0; 
    final_size = 0;
    EVP_DecryptInit(d_ctx, EVP_aes_128_gcm(), (const unsigned char *)session_key.data(), initialization_vector.data());
    EVP_DecryptUpdate(d_ctx, plain_buffer.data(), &actual_size, body.secure_data.data(),body.secure_data.size());
    EVP_CIPHER_CTX_ctrl(d_ctx, EVP_CTRL_GCM_SET_TAG,16,tag.common_mac.data());
    EVP_DecryptFinal(d_ctx, plain_buffer.data() + actual_size, &final_size);
    EVP_CIPHER_CTX_free(d_ctx);
    plain_buffer.resize(actual_size + final_size);

    return true;
}
        
bool AESGCMGMAC_Transform::preprocess_secure_submsg(
                DatawriterCryptoHandle &datawriter_crypto,
                DatareaderCryptoHandle &datareader_crypto,
                SecureSubmessageCategory_t &secure_submessage_category,
                const std::vector<uint8_t> encoded_rtps_submessage,
                const ParticipantCryptoHandle &receiving_crypto,
                const ParticipantCryptoHandle &sending_crypto,
                SecurityException &exception){

    exception = SecurityException("Not implemented");
    return false;
}

bool AESGCMGMAC_Transform::decode_datawriter_submessage(
                std::vector<uint8_t> &plain_rtps_submessage,
                const std::vector<uint8_t> &encoded_rtps_submessage,
                const DatareaderCryptoHandle &receiving_datareader_crypto,
                const DatawriterCryptoHandle &sending_datawriter_cryupto,
                SecurityException &exception){

    exception = SecurityException("Not implemented");
    return false;
}

bool AESGCMGMAC_Transform::decode_datareader_submessage(
                std::vector<uint8_t> &plain_rtps_submessage,
                const std::vector<uint8_t> &encoded_rtps_submessage,
                const DatawriterCryptoHandle &receiving_datawriter_crypto,
                const DatareaderCryptoHandle &sending_datareader_crypto,
                SecurityException &exception){

    exception = SecurityException("Not implemented");
    return false;
}


bool AESGCMGMAC_Transform::decode_serialized_payload(
                std::vector<uint8_t> &plain_buffer,
                const std::vector<uint8_t> &encoded_buffer,
                const std::vector<uint8_t> &inline_qos,
                DatareaderCryptoHandle &receiving_datareader_crypto,
                DatawriterCryptoHandle &sending_datawriter_crypto,
                SecurityException &exception){

    AESGCMGMAC_WriterCryptoHandle& sending_writer = AESGCMGMAC_WriterCryptoHandle::narrow(sending_datawriter_crypto);

    //Fun reverse order process;
    SecureDataHeader header;
    SecureDataBody body;
    SecureDataTag tag;

    //Header
    for(int i=0;i<4;i++) header.transform_identifier.transformation_kind.at(i) = ( encoded_buffer.at( i ) );
    for(int i=0;i<4;i++) header.transform_identifier.transformation_key_id.at(i) = ( encoded_buffer.at( i+4 ) );
    for(int i=0;i<4;i++) header.session_id.at(i) = ( encoded_buffer.at( i+8 ) );
    for(int i=0;i<8;i++) header.initialization_vector_suffix.at(i) = ( encoded_buffer.at( i+12 ) );
    //Body
    long body_length = 0;
    memcpy(&body_length, encoded_buffer.data()+20, sizeof(long));
    for(int i=0;i < body_length; i++) body.secure_data.push_back( encoded_buffer.at( i+20+sizeof(long) ) );
    //Tag
    for(int i=0;i < 16; i++) tag.common_mac.at(i) = ( encoded_buffer.at( i+20+sizeof(long)+body_length ) );
    
    uint32_t session_id;
    memcpy(&session_id,header.session_id.data(),4);
    
    //Sessionkey
    std::array<uint8_t,32> session_key = compute_sessionkey(
            sending_writer->Writer2ReaderKeyMaterial.at(0).master_sender_key,
            sending_writer->Writer2ReaderKeyMaterial.at(0).master_salt,
            session_id);
    //IV
    std::array<uint8_t,12> initialization_vector;
    memcpy(initialization_vector.data(), header.session_id.data(), 4);
    memcpy(initialization_vector.data() + 4, header.initialization_vector_suffix.data(), 8);

    
    OpenSSL_add_all_ciphers();
    RAND_load_file("/dev/urandom",32);

    EVP_CIPHER_CTX *d_ctx = EVP_CIPHER_CTX_new();
    plain_buffer.clear();
    plain_buffer.resize(encoded_buffer.size());

    int actual_size = 0, final_size = 0;
  
    OpenSSL_add_all_ciphers();
    RAND_load_file("/dev/urandom",32);

    d_ctx = EVP_CIPHER_CTX_new();
    plain_buffer.clear();
    plain_buffer.resize(encoded_buffer.size());

    bool return_value;
    EVP_DecryptInit(d_ctx, EVP_aes_128_gcm(), (const unsigned char *)session_key.data(), initialization_vector.data());
    EVP_DecryptUpdate(d_ctx, plain_buffer.data(), &actual_size, body.secure_data.data(),body.secure_data.size());
    EVP_CIPHER_CTX_ctrl(d_ctx, EVP_CTRL_GCM_SET_TAG,16,tag.common_mac.data());
    return_value = EVP_DecryptFinal(d_ctx, plain_buffer.data() + actual_size, &final_size);
    EVP_CIPHER_CTX_free(d_ctx);
    plain_buffer.resize(actual_size + final_size);

    return return_value;
}

std::array<uint8_t, 32> AESGCMGMAC_Transform::compute_sessionkey(std::array<uint8_t,32> master_sender_key,std::array<uint8_t,32> master_salt , uint32_t &session_id)
{

    std::array<uint8_t,32> session_key;
    unsigned char *source = (unsigned char*)malloc(32 + 10 + 32 + 2);
    memcpy(source, master_sender_key.data(), 32); 
    char seq[] = "SessionKey";
    memcpy(source+32, seq, 10);
    memcpy(source+32+10, master_salt.data(),32);
    memcpy(source+32+10+32, &(session_id),4);

    EVP_Digest(source, 32+10+32+2, (unsigned char*)&(session_key), NULL, EVP_sha256(), NULL);
    
    delete(source);
    return session_key;
}
