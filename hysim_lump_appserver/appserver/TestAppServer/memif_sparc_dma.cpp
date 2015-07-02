/* Authors: Yunsup Lee & Rimas Avizienis
 *          Parallel Computing Laboratory
 *          Electrical Engineering and Computer Sciences
 *          University of California, Berkeley
 *
 * Copyright (c) 2008, The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or withoutwa
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <Common/util.h>
#include <Common/host.h>

#include "memif_sparc_dma.h"
#include "dma.h"
#include "appserver.h"

#include <assert.h>

#include "CacheTag.h"

#define debug(str,...) do { } while(0)
#ifdef DEBUG_MODE
  #undef debug
  #define debug printf
#endif

#define HOST_ICACHE_SIZE 256
#define HOST_ICACHE_LINE_SIZE 32
#define HOST_ICACHE_LINES (HOST_ICACHE_SIZE/HOST_ICACHE_LINE_SIZE)
#define HOST_DCACHE_SIZE 16384
#define HOST_DCACHE_LINE_SIZE 32
#define HOST_DCACHE_LINES (HOST_DCACHE_SIZE/HOST_DCACHE_LINE_SIZE)
#define MAX_DMA_SIZE 512
#define MIN_THREADS 17
#define MAX_THREADS 64

#ifndef __linux__
# error This code only supports Linux
#endif

extern mem_response_packet previous;

memif_t::error
memif_sparc_dma_t::set_num_cores(int val)
{
  debug("memif_sparc_dma_t::set_num_cores(%d)\n",val);
  num_cores = val;
}

uint32_t memif_sparc_dma_t::read_chunk_align()
{
  return 4;
}

uint32_t memif_sparc_dma_t::read_chunk_min_size()
{
  return read_chunk_align();
}

uint32_t memif_sparc_dma_t::read_chunk_max_size()
{
  return MAX_DMA_SIZE;
}

uint32_t memif_sparc_dma_t::write_chunk_align()
{
  return 4;
}

uint32_t memif_sparc_dma_t::write_chunk_min_size()
{
  return write_chunk_align();
}

uint32_t memif_sparc_dma_t::write_chunk_max_size()
{
  return MAX_DMA_SIZE;
}

  memif_t::error
memif_sparc_dma_t::read_chunk(uint32_t addr, uint32_t len, uint8_t* bytes, uint8_t asi)
{
  memif_t::error res;
  uint32_t dma_buf[2048];

  debug("memif_sparc_dma_t::read_chunk(addr = %08x, len = %d, asi = %02x)\n",addr,len,asi);
  if(len % read_chunk_align() != 0 || addr % read_chunk_align() != 0)
    return memif_t::Misaligned;
  if(len > read_chunk_max_size())
    return memif_t::Invalid;

  for (int i = 0; i < len/sizeof(uint32_t); i++) 
  {
    dma_buf[i*2+0] = htonl(LOADALT_OPCODE(asi));
    dma_buf[i*2+1] = 0;
  }
  
  int ncores = asi == 0x1E ? (mt_init && reset_status ? MAX_THREADS : 1 /*roundup_pow2(num_cores)*/) : 1;
  // the length of the message
  int words = len/sizeof(uint32_t);
  // if the length i
  int word_interleaving = words >= ncores ? words/ncores : words;

  if (talk_to_dma(ncores, word_interleaving, addr, len*2, (uint8_t *)dma_buf, bytes, len) != -1)
    return memif_t::OK;
  return memif_t::Internal;
}

// bytes must be a multiple of four
memif_t::error
memif_sparc_dma_t::write_chunk(uint32_t addr, uint32_t len, const uint8_t* bytes, uint8_t asi)
{
  memif_t::error res;

  uint32_t write_len, i;
  uint32_t buf[1024];
  uint32_t read_buf[512];

  debug("memif_sparc_dma_t::write_chunk(addr = %08x, len = %d, asi = %02x)\n",addr,len,asi);
  if(len % write_chunk_align() != 0 || addr % write_chunk_align() != 0)
    return memif_t::Misaligned;
  if(len > write_chunk_max_size())
    return memif_t::Invalid;
  
  // the instruction words to execute
  for (i=0;i<len/sizeof(uint32_t);i++)
  {
    buf[i*2+0] = htonl(STOREALT_OPCODE(asi));
    buf[i*2+1] = *((unsigned int *)(bytes+i*4));
  }
  // in most cases ncors = 1 here, except that asi == 0x1E and initialize
  int ncores = asi == 0x1E ? (mt_init && reset_status ? MAX_THREADS : 1 /*roundup_pow2(num_cores)*/) : 1;
  int words = len/sizeof(uint32_t);
  int word_interleaving = words >= ncores ? words/ncores : words;
  // for each uint_32, have an instruction to read, len*2
  if (talk_to_dma(ncores, word_interleaving, addr, len*2, (uint8_t *)buf, 0, 0) == -1)
    return memif_t::Internal;
  return memif_t::OK;
}

memif_t::error
memif_sparc_dma_t::read_uint32(uint32_t addr, uint32_t* word, uint8_t asi)
{
  if(addr & 0x3)
    return memif_t::Misaligned;

  memif_t::error e = read_chunk(addr,sizeof(uint32_t),(uint8_t*)word,asi);
  *word = ntohl(*word);
  return e;
}

memif_t::error
memif_sparc_dma_t::write_uint32(uint32_t addr, uint32_t word, uint8_t asi)
{
  if(addr & 0x3)
    return memif_t::Misaligned;

  word = htonl(word);
  return read_chunk(addr,sizeof(uint32_t),(uint8_t*)&word,asi);
}

memif_sparc_dma_t::memif_sparc_dma_t()
{
  mac_configured = false;
  memset(seqno,0,sizeof(seqno));

  reset_status = 1;
  num_cores = 1;
  mt_init = true;
}

memif_sparchw_dma_t::memif_sparchw_dma_t(const char *_hw_addr, const char *_eth_device)
{
  memcpy(ramp_mac,_hw_addr,6);
  strcpy(eth_device, _eth_device);
  init();
}

memif_sparcvs_dma_t::memif_sparcvs_dma_t(const char* _host, int _port, const char* _hw_addr)
{
  strcpy(host, _host);
  port = _port;
  init();
}

memif_sparchw_dma_t::~memif_sparchw_dma_t()
{
  fini();
}

memif_sparcvs_dma_t::~memif_sparcvs_dma_t()
{
  fini();
}

void memif_sparchw_dma_t::init(void)
{
  memset(&myaddr, 0, sizeof(myaddr));

  // setuid root to open a raw socket.  if we fail, too bad
  seteuid(0);
  sock = socket(AF_PACKET, SOCK_RAW|SOCK_NONBLOCK, htons(ETH_P_ALL));
  seteuid(getuid());
  if(sock < 0)
    throw std::runtime_error("socket() failed!");

  myaddr.sll_ifindex = if_nametoindex(eth_device);
  myaddr.sll_family = AF_PACKET;

  int ret = bind(sock, (struct sockaddr *)&myaddr, sizeof(myaddr));
  if (ret < 0)
    throw std::runtime_error("bind() failed!");

  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 50000;
  if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(struct timeval)) < 0)
    throw std::runtime_error("setsockopt() failed!");

  // get MAC address of local ethernet device
  struct ifreq ifr;
  strcpy(ifr.ifr_name, eth_device);
  ret = ioctl(sock, SIOCGIFHWADDR, (char *)&ifr);
  if (ret < 0)
    throw std::runtime_error("ioctl() failed!");
  memcpy(&appsvr_mac, &ifr.ifr_ifru.ifru_hwaddr.sa_data, 6);
}

void memif_sparcvs_dma_t::init(void)
{
  struct sockaddr_in servername;
  struct hostent* hostinfo;

  /* Create the socket.   */
  sock = socket(PF_INET, SOCK_STREAM, 0);

  if(sock < 0)
    throw std::runtime_error("socket() failed");

  /* Connect to the server.   */
  servername.sin_family = AF_INET;
  servername.sin_port = htons(port);
  hostinfo = gethostbyname(host);

  if (hostinfo == NULL) 
    throw std::runtime_error("unknown host");

  servername.sin_addr = *(struct in_addr *)hostinfo->h_addr;

  if (connect(sock, (struct sockaddr *)&servername, sizeof(servername)) < 0)
    throw std::runtime_error("connect() failed");
}

void memif_sparchw_dma_t::fini(void)
{
  if (sock != -1)
    close(sock);
}

void memif_sparcvs_dma_t::fini(void)
{
  if (sock != -1)
    close(sock);
}

memif_t::error memif_sparc_dma_t::flush_cache(uint8_t nthreads)
{
  debug("flush_cache(nthreads = %d)\n",nthreads);

  int nlines_total = HOST_DCACHE_LINES;
  int nlines_per_packet = MAX_DMA_SIZE/sizeof(uint32_t);
  if(nlines_per_packet > nlines_total)
    nlines_per_packet = nlines_total;
  assert(nlines_total % nlines_per_packet == 0);
  assert(nlines_per_packet % nthreads == 0);
  static_assert(HOST_DCACHE_LINES % HOST_ICACHE_LINES == 0);
  int num_packets = nlines_total/nlines_per_packet;

  for(int packet = 0; packet < num_packets; packet++)
  {
    int size = 2*sizeof(uint32_t)*nlines_per_packet;
    uint32_t* buf = (uint32_t*)alloca(size);

    for(int i = 0; i < nlines_per_packet; i++)
    {
      int line = (i+nlines_per_packet/nthreads*packet)%nlines_per_packet + packet*nlines_per_packet;
      buf[2*i+0] = htonl(FLUSH_OPCODE);
      buf[2*i+1] = htonl(HOST_DCACHE_LINE_SIZE*line);
    }
  
    if(talk_to_dma(nthreads,nlines_per_packet/nthreads,0,size,(uint8_t*)buf,NULL,0))
      throw std::runtime_error("memif_sparc_dma::flush_cache()");
  }

  return memif_t::OK;
}

int memif_sparc_dma_t::execute_insn(uint8_t nthreads, uint32_t opcode, uint32_t data)
{
  int size = 2*sizeof(uint32_t)*nthreads;
  uint32_t* buf = (uint32_t*)alloca(size);
  
  for(int i = 0; i < nthreads; i++)
  {
    buf[2*i+0] = htonl(opcode);
    buf[2*i+1] = htonl(data);
  }
  // talk_to_dma(uint8_t nthreads, uint32_t interleaving, uint32_t addr, uint32_t len, const uint8_t* bytes, uint8_t* result, uint16_t read_len)
  // only one interleaves, addr=0
  if(talk_to_dma(nthreads,1,0,size,(uint8_t*)buf,NULL,0))
    throw std::runtime_error("memif_sparc_dma::execute_insn()");
  return memif_t::OK;
}


struct __attribute__ ((__packed__)) ramp_packet
{
  char dst_mac[6];
  char src_mac[6];
  unsigned short ethertype;
  unsigned short pad;

  unsigned char packettype;
  unsigned char pid;
  unsigned short seqno;

  char payload[MAX_PACKET_SIZE];
  // when we call send, only the first packetsize bytes are sent
  int packetsize;

  char expected_dst_mac[6];

  ramp_packet()
  {
    memset((char*)this,0,sizeof(*this));
    packetsize = 20;
  }

  void set_size(int sz)
  {
    assert(sz <= MAX_PACKET_SIZE);
    assert(sz % 4 == 0);

    packetsize = sz;
    if(packetsize < 60)
      pad = htons(packetsize-20)/4;
    else
      pad = 0xffff;
  }

  ramp_packet(const ramp_packet& p)
  {
    *this = p;
  }

  const ramp_packet& operator = (const ramp_packet& p)
  {
    memcpy((char*)this,(char*)&p,sizeof(*this));
    return *this;
  }

  ramp_packet(const char* dst_mac, const char* src_mac,
              char packettype, char pid, short seqno,
              const char* payload, int payloadsz)
  {
    // ethernet header
    memcpy(this->dst_mac,dst_mac,6);
    memcpy(this->expected_dst_mac,dst_mac,6);
    memcpy(this->src_mac,src_mac,6);
    this->ethertype = htons(RAMP_ETHERTYPE); // same for any endianness
    this->pad = 0;

    // ramp header
    this->packettype = packettype;
    this->pid = pid;
    this->seqno = htons(seqno);

    set_size(payloadsz+20);
    memset(this->payload,0,sizeof(this->payload));
    if(payload)
      memcpy(this->payload,payload,payloadsz);

  }

  int size()
  {
    return packetsize;
  }

  int payload_size()
  {
    return packetsize-20;
  }
};

int memif_sparc_dma_t::start_memory_transactions(uint8_t* recv)
{
	// send a start packet without payload
	// TODO: next_seqno(RAMP_PIPE_MEM)
	ramp_packet p(ramp_mac,appsvr_mac,RAMP_TYPE_MEM_START,0,next_seqno(0),NULL,0);
	// printf("Sending MEM_START packet...\n");
	send_packet(&p);
	memcpy(recv, p.payload, p.payload_size());
	// printf("Got %d bytes response\n", p.payload_size());
	return p.payload_size();
}

int memif_sparc_dma_t::send_memory_transactions(uint8_t* recv, const uint8_t* payload, uint16_t payload_len, uint16_t recv_len)
{
	// send a start packet with the given payload
	// TODO: next_seqno(RAMP_PIPE_MEM)
	ramp_packet p(ramp_mac,appsvr_mac,RAMP_TYPE_MEM,0,next_seqno(0),(char*)payload,payload_len);
	// printf("Sending %d bytes MEM packet...\n", payload_len);
	send_packet(&p);
	memcpy(recv, p.payload, recv_len < p.payload_size() ? recv_len : p.payload_size());
	// printf("Got %d bytes response\n", p.payload_size());
	return p.payload_size();
}

int memif_sparc_dma_t::talk_to_dma(uint8_t nthreads, uint32_t interleaving, uint32_t addr, uint32_t len, const uint8_t* bytes, uint8_t* result, uint16_t read_len)
{
  dma_command_t *command;
  int nbytes, tid, offset;

  assert(nthreads && ((nthreads-1)&nthreads)==0);
  assert(interleaving >= 1 && interleaving <= MAX_DMA_SIZE/sizeof(uint32_t));
  debug("memif_sparchw_dma_t::talk_to_dma(addr = %08x, len = %d)\n",addr,len/2);
  addr /= sizeof(uint32_t);

  // write data packet to FPGA DMA buffer

  ramp_packet p(ramp_mac,appsvr_mac,RAMP_TYPE_DATA,0,
                next_seqno(0),NULL,len);
 
  memcpy(p.payload,bytes,len);
  send_packet(&p);

  len = len / 8; // number of actual data words we are trying to write
  int ncommands = (len+interleaving-1)/interleaving; // how many interleaved command groups
  // data in len are for [core0...(=interleaving)] [core1] [core2]...
  nthreads = std::min((int)nthreads,ncommands);
  ramp_packet p2(ramp_mac,appsvr_mac,RAMP_TYPE_COMMAND,0,
                 next_seqno(0),NULL,ncommands*8+4); // each command is 8 bytes (2 double words)
  
  uint32_t* payload = (uint32_t*)p2.payload;
  // COMMAND (p2):
  // byte 0: number of threads (high 16 bits) + number of double words (low 16 bits)
  payload[0] = htonl(nthreads << 16 | read_len >> 2);
  offset = 0;
  for(int i = 0; i < ncommands; i++) // commands for each core
  {
    int tid = i % nthreads;	// interleaved!
    int this_len = MIN(interleaving,len);	// len for this interleaved group
    // the address for each instr sent in the first packet p
    payload[2*i+1] = htonl(addr+offset);
    // tid = thread id, offset is the write addr
    payload[2*i+2] = htonl((tid << 20) | (offset << 10) | (this_len-1));
    len -= this_len;
    offset += this_len;

    if(i == ncommands-1)
      assert(len == 0);
  }
  // this will also wait for the response
  send_packet(&p2);
  if(p2.payload_size() != MAX(read_len, 40)) {
    debug("response size = %d, expected =%d\n", p2.payload_size(), read_len);
    throw std::runtime_error("incorrect response packet size!");
  }  
  if(read_len)
    memcpy(result, p2.payload, read_len);

  return 0;
}

void memif_sparc_dma_t::send_packet(ramp_packet* packet)
{
  ramp_packet response;
  #if 0
  if(packet->packettype == RAMP_TYPE_COMMAND)
  {
	uint8_t p[4] = {0x00, 0x00, 0x00, 0x06};
  	uint8_t recv[16];
  	send_memory_transactions(recv, p, sizeof(p), 16);
  }
  #endif
  for(int i = 0; i < 10; i++)
  {
    
    int ret = do_send((char*)packet,packet->size());
    #if 0
    if(packet->packettype == RAMP_TYPE_COMMAND)
    {
	uint8_t p[4] = {0x00, 0x00, 0x00, 0x07};
  	uint8_t recv[16];
  	send_memory_transactions(recv, p, sizeof(p), 16);
    }
    #endif
    if(ret != packet->size())
      continue;
    
    debug("wait for response \n");
    //usleep(2);
    int counterer=0;
    while(1)
    {
      ret = ::read(sock,(char*)&response,MAX_PACKET_SIZE);
      
      if(ret == -1)
      {
        debug("timeout\n");
	counterer+=1;  
      #if 0
        if((packet->packettype == RAMP_TYPE_DATA || packet->packettype == RAMP_TYPE_COMMAND) && i > 5)
	{
	  /**uint8_t p[4] = {0x00, 0x00, 0x00, 0x06};
	  uint8_t recv[16];
	  send_memory_transactions(recv, p, sizeof(p), 16);**/
	  mem_response_packet pa;
	  pa.payload = previous.payload;
	  uint8_t recv_new[RAMP_MEM_PACKET_LEN] = {0};
	  *((uint32_t*)recv_new) = htonl(pa.payload);
	  int recv_len = send_memory_transactions(recv_new, recv_new, RAMP_MEM_RESPONSE_LEN, RAMP_MEM_PACKET_LEN);//could cause recursion issue
	}
	#endif
	
	if(counterer>=50000){
		printf("true timeout\n");
		break;
	}
	else
		continue;
	

        //break;
      }
      if(response.ethertype != htons(RAMP_ETHERTYPE) || memcmp(response.src_mac,packet->expected_dst_mac,6) != 0)
        continue;
      else
	break;
    }
    if(ret == -1)
      continue;
    response.set_size(ret);

    debug("got %d bytes: \n",ret);
    for(int i = 0; i < ret; i++)
      debug("%02x ",(unsigned int)(unsigned char)((char*)&response)[i]);
    debug("\n");

    // if we didn't send a broadcast packet, verify sender's mac
    if(response.packettype != RAMP_TYPE_ACK && response.packettype != RAMP_TYPE_MEM_ACK && response.packettype != RAMP_TYPE_POLL_DELAY) {
      debug("packet type wrong\n");
      printf("packet type wrong\n");
      printf("packettype:%X\n", response.packettype);
      continue;
   }
    if(response.seqno != packet->seqno) 
      continue;
    debug("got something good\n");
    bool poll_delay_bool;
    if(response.packettype != RAMP_TYPE_POLL_DELAY) {
	//need to tell appserver run method to not poll for a bit
	//also need to set response to all 0s (size of magic mem which is currently uint8_t [40]
	poll_delay_bool = true;
	//not messing with response for now - just get poll to return and not operate on the data
    }
    else {
	poll_delay_bool = false;
    }

    *packet = response;
    return;
  }

  throw std::runtime_error("memif_sparc_dma_t::send_packet(): timeout");
}

int memif_sparchw_dma_t::do_send(const char* packet, int size)
{ 
  debug("send packet size = %d\n", size);
  for (int i=0;i<size;i++)
	debug("%02X ", *((unsigned char*)(packet+i))); 
  debug("\n");
 
 return ::sendto(sock,packet,size,0,(sockaddr*)&myaddr,sizeof(myaddr));
}

int memif_sparcvs_dma_t::do_send(const char* packet, int size)
{
  debug("send packet size = %d\n", size);
  for (int i=0;i<size;i++)
    debug("%02X ", *((unsigned char*)(packet+i))); 
  debug("\n");
  return ::write(sock,packet,size);
}

void memif_sparc_dma_t::stop(void)
{
  set_run(0,1);
}

short memif_sparc_dma_t::next_seqno(uint8_t pipeline)
{
  return ++seqno[pipeline];
}

void memif_sparc_dma_t::latency_test(void)
{
  uint8_t p[4] = {0x00, 0x00, 0x00, 0x07};
  uint8_t recv[16];
  timespec ts_start, ts_end;
  int i;
  unsigned long     time_difference; // must be unsigned to allow underflow
  
  clock_getres( CLOCK_REALTIME, &ts_start);
  printf("Resolution is %f nano seconds.\n", ts_start.tv_nsec / 1000.0 );
  
  /* Step 1: packet only, no processing */
  // warmup
  for(i = 0; i < 16; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  }
  clock_gettime(CLOCK_REALTIME, &ts_start); 
  for(i = 0; i < 1000; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  }
  clock_gettime(CLOCK_REALTIME, &ts_end);
  
  time_difference = ts_end.tv_nsec - ts_start.tv_nsec;
  printf("Elapsed time: %ld ns\n", time_difference );
  
  /* Step 2: all access hit */
  CCacheTag* TestL1Cache = new CCacheTag(64, 4, 128);
  CCacheTag* TestL2Cache = new CCacheTag(64, 4, 1024);
  TestL1Cache->MissDelay(3);
  TestL2Cache->MissDelay(10);
  TestL1Cache->SetNextLevel(*TestL2Cache);
  TestL1Cache->Access(0);
  // warmup
  for(i = 0; i < 16; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  	TestL1Cache->Access(0);
  }
  clock_gettime(CLOCK_REALTIME, &ts_start); 
  for(i = 0; i < 1000; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  	int t = TestL1Cache->Access(0);
  	// printf("Access time: %d\n", t);
  }
  clock_gettime(CLOCK_REALTIME, &ts_end);
  
  time_difference = ts_end.tv_nsec - ts_start.tv_nsec; // this might underflow
  printf("Elapsed time: %ld ns. Cache Access time %d\n", time_difference, TestL1Cache->Access(0));
  
  delete TestL1Cache;
  delete TestL2Cache;
  
  /* Step 3: all access miss */
  TestL1Cache = new CCacheTag(64, 4, 128);
  TestL2Cache = new CCacheTag(64, 4, 1024);
  TestL1Cache->MissDelay(3);
  TestL2Cache->MissDelay(10);
  TestL1Cache->SetNextLevel(*TestL2Cache);
  
  // warmup
  for(i = 0; i < 16; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  	TestL1Cache->Access(i*64);
  }
  clock_gettime(CLOCK_REALTIME, &ts_start); 
  for(i = 0; i < 1000; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  	int t = TestL1Cache->Access((i+16)*64);
  	// printf("Access time: %d\n", t);
  }
  clock_gettime(CLOCK_REALTIME, &ts_end);
  
  time_difference = ts_end.tv_nsec - ts_start.tv_nsec; 
  printf("Elapsed time: %ld ns. Cache Access time %d\n", time_difference, TestL1Cache->Access(0));
  
  delete TestL1Cache;
  delete TestL2Cache;
  
  /* Step 3: all access replace */
  TestL1Cache = new CCacheTag(64, 4, 128);
  TestL2Cache = new CCacheTag(64, 4, 1024);
  TestL1Cache->MissDelay(3);
  TestL2Cache->MissDelay(10);
  TestL1Cache->SetNextLevel(*TestL2Cache);
  
  // warmup
  for(i = 0; i < 16; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  	TestL1Cache->Access(i*1024*64);
  }
  clock_gettime(CLOCK_REALTIME, &ts_start); 
  for(i = 0; i < 1000; ++i)
  {
  	send_memory_transactions(recv, p, sizeof(p), 16);
  	int t = TestL1Cache->Access((i+16)*1024*64);
  	// printf("Access time: %d\n", t);
  }
  clock_gettime(CLOCK_REALTIME, &ts_end);
  
  time_difference = ts_end.tv_nsec - ts_start.tv_nsec;
  printf("Elapsed time: %ld ns. Cache Access time %d\n", time_difference, TestL1Cache->Access(0));
  
  delete TestL1Cache;
  delete TestL2Cache;
  
  
}

memif_t::error memif_sparc_dma_t::set_run(int run, int threads_active)
{
  // try to reset.  if that fails, then send a broadcast packet
  // to assign the MAC address to some board.
  char payload[12];
  payload[0] = std::max(threads_active,MIN_THREADS)-1;
  payload[1] = threads_active-1;
  payload[2] = !!run;
  payload[3] = 0; // padding
  ramp_packet start(ramp_mac,appsvr_mac,RAMP_TYPE_TIMING,
                   RAMP_PIPE_RESET,next_seqno(RAMP_PIPE_RESET),payload,4);

  ramp_packet reset(ramp_mac,appsvr_mac,RAMP_TYPE_RESET,
                    RAMP_PIPE_RESET,next_seqno(RAMP_PIPE_RESET),NULL,0);

  ramp_packet* packet = run ? &start : &reset;

  try
  {
    // if using verilog simulator, configure mac first
    if(broadcast_first() && !mac_configured)
      throw std::runtime_error("");
    printf("sending start packet...\n");
    send_packet(packet);
  }
  catch(std::runtime_error& e)
  {
    if(mac_configured)
      throw;

    memcpy(payload,appsvr_mac,6);
    memcpy(payload+6,ramp_mac,6);
    #if 1
    ramp_packet init(broadcast_mac,appsvr_mac,RAMP_TYPE_RESET,
                     RAMP_PIPE_MAC,next_seqno(RAMP_PIPE_MAC),payload,12);
    memcpy(init.expected_dst_mac,ramp_mac,6);
    printf("sending init packet...\n");
    send_packet(&init);
    #endif
    mac_configured = true;

    send_packet(packet);
  }
  #if 0
  uint8_t recv[4096];
  printf("starting mem transactions...\n");
  start_memory_transactions(recv);
  // memset(recv, 0x0, 4096);
  // memset(recv+15, 0x10, 1);
  // recv[15] = 0x10;
  send_memory_transactions(recv, recv, 16);
  printf("Done...\n");
  exit(0);
  #endif
  
  uint8_t p[4] = {0x00, 0x00, 0x00, 0x07};
  uint8_t recv[16];
  //send_memory_transactions(recv, p, sizeof(p), 16);
  // latency_test();
  
  return memif_t::OK;
} 

memif_t::error memif_sparc_dma_t::start(void)
{
  printf("Set memsize\n");
  execute_insn(MAX_THREADS,WRASR_OPCODE(13),memsize>>20); // asr13 = memsize MB
  printf("Set nthreads\n");
  execute_insn(MAX_THREADS,WRASR_OPCODE(14),num_cores); // asr14 = nthreads
  printf("Flush TLB\n");
  execute_insn(MAX_THREADS,STOREALT_OPCODE(3),0x400); // flush TLB
  printf("Flush Cache\n");
  flush_cache(MAX_THREADS);
  printf("call set_run()\n");
  memif_t::error e = set_run(1,num_cores);
  printf("started!\n");
  reset_status = 0;
  return e;
}
