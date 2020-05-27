#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <testsuite.h>
#include <uk/assert.h>
#include "measure.h"
#include "pktgen.h"
#include "dev.h"
#include <uk/netdev.h>
#include <assert.h>


#define PRINT_ITERATION		10
#define RX_NUM_BUF 16384
#define TX_NUM_BUF 16384

char keys[256][256];

#define MAP_MISSING -3  /* No such element */
#define MAP_FULL -2 	/* Hashmap is full */
#define MAP_OMEM -1 	/* Out of Memory */
#define MAP_OK 0 	/* OK */

#define INITIAL_SIZE (256)
#define MAX_CHAIN_LENGTH (8)

typedef void *any_t;
/* We need to keep keys and values */
typedef struct _hashmap_element{
	char* key;
	int in_use;
	any_t data;
} hashmap_element;

/* A hashmap has some maximum size and current size,
 *  * as well as the data to hold. */
typedef struct _hashmap_map{
	int table_size;
	int size;
	hashmap_element *data;
} hashmap_map;

typedef any_t map_t;
typedef int (*PFany)(any_t, any_t);


map_t hashmap_new();
void hashmap_free(map_t in);
int hashmap_put(map_t in, char* key, any_t value);
int hashmap_length(map_t in);

/*
 *  * Return an empty hashmap, or NULL on failure.
 *   */
map_t hashmap_new() {
	hashmap_map* m = (hashmap_map*) malloc(sizeof(hashmap_map));
	if(!m) goto err;

	m->data = (hashmap_element*) calloc(INITIAL_SIZE, sizeof(hashmap_element));
	if(!m->data) goto err;

	m->table_size = INITIAL_SIZE;
	m->size = 0;

	return m;
	err:
		if (m)
			hashmap_free(m);
		return NULL;
}

static unsigned long crc32_tab[] = {
      0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
      0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
      0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
      0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
      0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
      0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
      0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
      0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
      0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
      0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
      0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
      0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
      0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
      0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
      0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
      0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
      0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
      0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
      0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
      0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
      0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
      0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
      0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
      0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
      0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
      0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
      0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
      0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
      0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
      0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
      0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
      0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
      0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
      0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
      0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
      0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
      0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
      0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
      0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
      0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
      0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
      0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
      0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
      0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
      0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
      0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
      0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
      0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
      0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
      0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
      0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
      0x2d02ef8dL
   };

/* Return a 32-bit CRC of the contents of the buffer. */
unsigned long crc32(const unsigned char *s, unsigned int len)
{
  unsigned int i;
  unsigned long crc32val;
  
  crc32val = 0;
  for (i = 0;  i < len;  i ++)
    {
      crc32val =
	crc32_tab[(crc32val ^ s[i]) & 0xff] ^
	  (crc32val >> 8);
    }
  return crc32val;
}

/*
 *  * Hashing function for a string
 *   */
unsigned int hashmap_hash_int(hashmap_map * m, char* keystring){

    unsigned long key = crc32((unsigned char*)(keystring), strlen(keystring));

	/* Robert Jenkins' 32 bit Mix Function */
	key += (key << 12);
	key ^= (key >> 22);
	key += (key << 4);
	key ^= (key >> 9);
	key += (key << 10);
	key ^= (key >> 2);
	key += (key << 7);
	key ^= (key >> 12);

	/* Knuth's Multiplicative Method */
	key = (key >> 3) * 2654435761;

	return key % m->table_size;
}

/*
 *  * Return the integer of the location in data
 *   * to store the point to the item, or MAP_FULL.
 *    */
int hashmap_hash(map_t in, char* key){
	int curr;
	int i;

	/* Cast the hashmap */
	hashmap_map* m = (hashmap_map *) in;

	/* If full, return immediately */
	if(m->size >= (m->table_size/2)) return MAP_FULL;

	/* Find the best index */
	curr = hashmap_hash_int(m, key);

	/* Linear probing */
	for(i = 0; i< MAX_CHAIN_LENGTH; i++){
		if(m->data[curr].in_use == 0)
			return curr;

		if(m->data[curr].in_use == 1 && (strcmp(m->data[curr].key,key)==0))
			return curr;

		curr = (curr + 1) % m->table_size;
	}

	return MAP_FULL;
}

/*
 *  * Doubles the size of the hashmap, and rehashes all the elements
 *   */
int hashmap_rehash(map_t in){
	int i;
	int old_size;
	hashmap_element* curr;

	/* Setup the new elements */
	hashmap_map *m = (hashmap_map *) in;
	hashmap_element* temp = (hashmap_element *)
		calloc(2 * m->table_size, sizeof(hashmap_element));
	if(!temp) return MAP_OMEM;

	/* Update the array */
	curr = m->data;
	m->data = temp;

	/* Update the size */
	old_size = m->table_size;
	m->table_size = 2 * m->table_size;
	m->size = 0;

	/* Rehash the elements */
	for(i = 0; i < old_size; i++){
        int status;

        if (curr[i].in_use == 0)
            continue;
            
		status = hashmap_put(m, curr[i].key, curr[i].data);
		if (status != MAP_OK)
			return status;
	}

	free(curr);

	return MAP_OK;
}

/*
 *  * Add a pointer to the hashmap with some key
 *   */
int hashmap_put(map_t in, char* key, any_t value){
	int index;
	hashmap_map* m;

	/* Cast the hashmap */
	m = (hashmap_map *) in;

	/* Find a place to put our value */
	index = hashmap_hash(in, key);
	while(index == MAP_FULL){
		if (hashmap_rehash(in) == MAP_OMEM) {
			return MAP_OMEM;
		}
		index = hashmap_hash(in, key);
	}

	/* Set the data */
	m->data[index].data = value;
	m->data[index].key = key;
	m->data[index].in_use = 1;
	m->size++; 

	return MAP_OK;
}

/*
 *  * Get your pointer out of the hashmap with a key
 *   */
int hashmap_get(map_t in, char* key, any_t *arg){
	int curr;
	int i;
	hashmap_map* m;

	/* Cast the hashmap */
	m = (hashmap_map *) in;

	/* Find data location */
	curr = hashmap_hash_int(m, key);

	/* Linear probing, if necessary */
	for(i = 0; i<MAX_CHAIN_LENGTH; i++){

        int in_use = m->data[curr].in_use;
        if (in_use == 1){
            if (strcmp(m->data[curr].key,key)==0){
                *arg = (m->data[curr].data);
                return MAP_OK;
            }
		}

		curr = (curr + 1) % m->table_size;
	}

	*arg = NULL;

	/* Not found */
	return MAP_MISSING;
}

/*
 *  * Iterate the function parameter over each element in the hashmap.  The
 *   * additional any_t argument is passed to the function as its first
 *    * argument and the hashmap element is the second.
 *     */
int hashmap_iterate(map_t in, PFany f, any_t item) {
	int i;

	/* Cast the hashmap */
	hashmap_map* m = (hashmap_map*) in;

	/* On empty hashmap, return immediately */
	if (hashmap_length(m) <= 0)
		return MAP_MISSING;	

	/* Linear probing */
	for(i = 0; i< m->table_size; i++)
		if(m->data[i].in_use != 0) {
			any_t data = (any_t) (m->data[i].data);
			int status = f(item, data);
			if (status != MAP_OK) {
				return status;
			}
		}

    return MAP_OK;
}

/*
 *  * Remove an element with that key from the map
 *   */
int hashmap_remove(map_t in, char* key){
	int i;
	int curr;
	hashmap_map* m;

	/* Cast the hashmap */
	m = (hashmap_map *) in;

	/* Find key */
	curr = hashmap_hash_int(m, key);

	/* Linear probing, if necessary */
	for(i = 0; i<MAX_CHAIN_LENGTH; i++){

        int in_use = m->data[curr].in_use;
        if (in_use == 1){
            if (strcmp(m->data[curr].key,key)==0){
                /* Blank out the fields */
                m->data[curr].in_use = 0;
                m->data[curr].data = NULL;
                m->data[curr].key = NULL;

                /* Reduce the size */
                m->size--;
                return MAP_OK;
            }
		}
		curr = (curr + 1) % m->table_size;
	}
	/* Data not found */
	return MAP_MISSING;
}


/* Return the length of the hashmap */
int hashmap_length(map_t in){
	hashmap_map* m = (hashmap_map *) in;
	if(m != NULL) return m->size;
	else return 0;
}

/* Deallocate the hashmap */
void hashmap_free(map_t in){
	hashmap_map* m = (hashmap_map*) in;
	free(m->data);
	free(m);
}


#define KEY_MAX_LENGTH (255)
#define KEY_PREFIX ("somekey")
#define KEY_COUNT (1024 * 128 )

typedef struct data_struct_s
{
    char key_string[KEY_MAX_LENGTH];
    int number;
} data_struct_t;

map_t mymap;
char key_string[KEY_MAX_LENGTH];
data_struct_t *value;

void init_hashmap()
{
	int index;
	int error;
	mymap = hashmap_new();
	value = malloc(sizeof(data_struct_t));

	for (index=0; index<256; index+=1)
	{
		/* Store the key string along side the numerical value so we can free it later */
		value = malloc(sizeof(data_struct_t));
		snprintf(value->key_string, KEY_MAX_LENGTH, "%s%d", KEY_PREFIX, index);
		value->number = index;

		error = hashmap_put(mymap, value->key_string, value);
		assert(error==MAP_OK);
	}

	for (index=0; index<KEY_COUNT; index+=1)
	{
		/* Store the key string along side the numerical value so we can free it later */
		value = malloc(sizeof(data_struct_t));
		snprintf(value->key_string, KEY_MAX_LENGTH, "%d", KEY_PREFIX, index);
		value->number = index;

		error = hashmap_put(mymap, value->key_string, value);
		assert(error==MAP_OK);
	}
}

void do_processing(char * t)
{
    int error;
    int index = (int)t[0];
    //snprintf(key_string, KEY_MAX_LENGTH, "%s%d", KEY_PREFIX, index);
    //error = hashmap_get(mymap, key_string, (void**)(&value));
    error = hashmap_get(mymap, keys[index], (void**)(&value));
    
}


static uint64_t	arg_stat_timer =  1000 * TIMER_MILLISECOND; /* default period is ~ 1s */
static uint64_t	arg_time_out = 100 * TIMER_MILLISECOND; /* default period is ~ 10 seconds */
/* 2 minutes Duration */
static uint64_t	arg_exp_end_time = 10 * 1000 * TIMER_MILLISECOND;
static uint32_t	arg_src_ipv4_addr = IPv4(172, 18, 0, 4);
static uint32_t	arg_dst_ipv4_addr = IPv4(172, 18, 0, 2);
static uint32_t arg_print_itr = 10;
static uint16_t	arg_src_port = 9000;
static uint16_t	arg_dst_port = 9001;
static int  burst_cnt = DEF_PKT_BURST;
static struct rte_ether_addr arg_dst_mac = {{0x68,0x05,0xCA,0x0C,0x47,0x51}};

void dump_stats(int iter)
{
	int i;
	/**
	 * Printing packets latency per second.
	 */
	printf("The latency(iteration):count of packet to received/transmitted per sec\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu(%llu):%llu,%llu\n", pkt_stats.latency[i],
			pkt_stats.rxburst_itr[i], pkt_stats.total_rxpkts_pps[i], pkt_stats.rxpkt_dropped[i]);
	}

	/**
	 * printing total packets
	 */
	printf("The count of packet to received: %llu\n",
		pkt_stats.total_rxpkts);

#if 0
	printf("The count of packet to transmitted/NR of packet sent to virtio per sec/Total iteration\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu/%llu/%llu/%llu/%llu/%llu\n",
		       pkt_stats.total_txpkts_pps[i],
		       pkt_stats.total_txpkts_tries_pps[i],
		       pkt_stats.total_txpkts_tries_ra_pps[i],
		       pkt_stats.total_txpkts_tries_dropped[i],
		       pkt_stats.total_txpkts_alloc_failed[i],
		       pkt_stats.txburst_itr[i]);
	}
#if 0
	pkt_stats.avg_txpkt_gen_cycles = 0;
	pkt_stats.avg_txpkt_buf_cycles = 0;
	pkt_stats.avg_txpkt_xmit_cycles = 0;
#endif

	printf("The latency of pkt alloc/pkt gen/ pkt send\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu/%llu/%llu\n",
			pkt_stats.txpkt_buf_cycles[i],
			pkt_stats.txpkt_gen_cycles[i],
			pkt_stats.txpkt_xmit_cycles[i]);
	}
#endif

	printf("The latency of pkt recv/0 pkt recv/pkt process/ pkt free\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu/%llu/%llu/%llu\n",
			pkt_stats.rxpkt_recv_cycles[i],
			pkt_stats.rxpkt_zrecv_cycles[i],
			pkt_stats.rxpkt_process_cycles[i],
			pkt_stats.rxpkt_buf_free[i]);
	}
}

static int64_t echo_packet(struct rte_mbuf *mbuf)
{
	uint16_t offset = 0;
	int rc = -1;
	struct rte_ether_hdr *eth_header = (struct ether_header *)
		rte_pktmbuf_mtod_offset(mbuf, char *, offset);

	struct rte_ether_addr eth_addr;
	struct rte_ipv4_hdr *ip_hdr;
	struct rte_udp_hdr *udp_hdr;
	struct request_header *rsq_hdr;
	int64_t oneway_latency;
	uint32_t ip_addr;
	uint16_t port_addr;
	char *data_ptr;		

	if (eth_header->ether_type == 0x8) {
		offset += sizeof(struct rte_ether_hdr);
		ip_hdr = rte_pktmbuf_mtod_offset(mbuf, char *, offset);
		//rte_eth_macaddr_get(0, &addr);
		if (ip_hdr->next_proto_id == 0x11) {
			offset += sizeof(struct rte_ipv4_hdr);

			rte_ether_addr_copy(&eth_header->d_addr, &eth_addr);
			rte_ether_addr_copy(&eth_header->s_addr, &eth_header->d_addr);
			rte_ether_addr_copy(&eth_addr, &eth_header->s_addr);

			/* Switch IP addresses */
			ip_hdr->src_addr ^= ip_hdr->dst_addr;
			ip_hdr->dst_addr ^= ip_hdr->src_addr;
			ip_hdr->src_addr ^= ip_hdr->dst_addr;

			/* switch UDP PORTS */
			udp_hdr->src_port ^= udp_hdr->dst_port;
			udp_hdr->dst_port ^= udp_hdr->src_port;
			udp_hdr->src_port ^= udp_hdr->dst_port;

			rc = 0;
			/* No checksum requiere, they are 16 bits and
			 * switching them does not influence the checsum
			 * PS: I have also computed the cheksum and it's the same
			 * */
			offset += sizeof(struct rte_udp_hdr);
			data_ptr = rte_pktmbuf_mtod_offset(mbuf, char *, offset);
			do_processing(data_ptr);


		}
	}

	return rc;
}

int pkt_burst_receive_cnt(int port_id, struct rte_mempool *mpool, int pkt_cnt,
			  int idx)
{
	int pkt = 0;
	uint64_t pkt_sent = 0;
	int i;

	uint64_t start_tsc, end_tsc;
	uint64_t curr_tsc;
#ifdef CONFIG_USE_DPDK_PMD
	struct rte_mbuf *bufs[DEF_PKT_BURST];
#else
	struct uk_netbuf *bufs[DEF_PKT_BURST];
#endif
	uint64_t latency = 0;
	int rc;


#ifndef CONFIG_USE_DPDK_PMD
	struct rte_eth_dev *vrtl_eth_dev;
	struct uk_ethdev_private *dev_private;
	vrtl_eth_dev = &rte_eth_devices[port_id];
	UK_ASSERT(vrtl_eth_dev);
	dev_private = vrtl_eth_dev->data->dev_private;
	UK_ASSERT(dev_private);
#endif
	while(pkt == 0)
		pkt = pkt_burst_receive(port_id, mpool, &bufs[0], DEF_PKT_BURST,
			 &latency, idx);

#ifdef CONFIG_USE_DPDK_PMD
	for (i = 0; i < pkt; i++) {
		echo_packet(bufs[i]);
	}
	pkt_sent = rte_eth_tx_burst(port_id, 0, &bufs[0], pkt)
#else
	latency = 0;
	for (i = 0; i < pkt; i++) {
		echo_packet(bufs[i]->priv);
	}
	pkt_sent = pkt;
        rc = uk_netdev_tx_burst(dev_private->netdev,
                                0, &bufs[0], &pkt_sent);
	if (unlikely(rc < 0 || pkt_sent < pkt)) {
		//pkt_stats.rxpkt_dropped[idx] += (pkt - pkt_sent);
		for (i = pkt_sent; i < pkt; i++)
			rte_pktmbuf_free(bufs[i]->priv);
	}
	//pkt_stats.latency[idx] += latency;
#endif
	//end_tsc = rte_rdtsc();
	//pkt_stats.rxpkt_buf_free[idx] += (end_tsc - start_tsc);
	//latency = 0;
	//pkt_stats.rxburst_itr[idx]++;

	return pkt;
}

int pkt_burst_transmit_cnt(int port_id, struct rte_mempool *mp, int cnt, int itr)
{
	int pkts = 0;
	int tx_cnt = 0;

	//while (pkts < cnt) {
		tx_cnt  = pkt_burst_transmit(port_id, mp, itr);
		pkts += tx_cnt;
	//}
	return pkts;
}


static int test_udpecho(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned nb_ports;
	uint16_t portid, first_portid = 0xff;
	int i;

	init_hashmap();
	for (int i = 0; i < 256; i++) {
                   snprintf(keys[i], 256, "%s%d", KEY_PREFIX, i);
       }

#if 0
	/* Application args process */
	int ret = test_app_args(argc, argv);
#endif

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		UK_CRASH("Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	/* Check that there is an even number of ports to send/receive on. */
	nb_ports = rte_eth_dev_count_avail();
	printf("%d number of ports detected Pool Size: %d\n", nb_ports,
		RTE_MBUF_DEFAULT_BUF_SIZE);

	/* Creates a new mempool in memory to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
					    RTE_MEMPOOL_CACHE_MAX_SIZE, 0,
					    RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());


	/* Initialize all ports. */
	RTE_ETH_FOREACH_DEV(portid) {
		if (first_portid == 0xff)
			first_portid = portid;
		if (port_init(portid, mbuf_pool) != 0)
			UK_CRASH("Cannot init port %"PRIu16 "\n",
					portid);
	}

	tx_pkt_setup(arg_src_ipv4_addr, arg_dst_ipv4_addr,
		     arg_src_port, arg_dst_port);

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

	/* Call lcore_main on the master core only. */
	i = 0;
	int tx_count = 0, total_tx_count = 0;
	int rx_count = 0, total_rx_count = 0;
	uint64_t start_tsc = rte_rdtsc(); 
	uint64_t curr_tsc;
	uint64_t latency;
	uint64_t j = 0;
	int cnt = burst_cnt;
	uint64_t exp_start = start_tsc, exp_curr, dur = 0;
	do {
		while (dur < arg_exp_end_time) {
			//tx_count = pkt_burst_transmit_cnt(first_portid, mbuf_pool_tx, cnt, j);
			//pkt_stats.total_txpkts_pps[j] += tx_count;
			while (1)
				rx_count = pkt_burst_receive_cnt(first_portid, mbuf_pool, cnt, j);
			pkt_stats.total_rxpkts_pps[j] += rx_count;
			pkt_stats.txburst_itr[j]++;
			curr_tsc = rte_rdtsc();
			dur = curr_tsc - exp_start;

			if (curr_tsc - start_tsc > arg_stat_timer) {
				pkt_stats.total_rxpkts += pkt_stats.total_rxpkts_pps[j];
				pkt_stats.total_txpkts += pkt_stats.total_txpkts_pps[j];
				j++;
#if 0
				if (j == arg_print_itr) {
					/* Print stat Computation */
					dump_stats(j);
					j = 0;
				}
#endif
				/* Resetting the tx count */
				pkt_stats.total_txpkts_pps[j] = 0;
				pkt_stats.total_rxpkts_pps[j] = 0;
				pkt_stats.latency[j] = 0;
				pkt_stats.rxburst_itr[j] = 0;
				pkt_stats.txburst_itr[j] = 0;
				pkt_stats.total_txpkts_tries_pps[j] = 0;
				pkt_stats.total_txpkts_tries_dropped[j] = 0;
				pkt_stats.total_txpkts_tries_ra_pps[j] = 0;
				pkt_stats.total_txpkts_alloc_failed[j] = 0;
				pkt_stats.txpkt_xmit_cycles[j] = 0;
				pkt_stats.txpkt_gen_cycles[j] = 0;
				pkt_stats.txpkt_buf_cycles[j] = 0;
				pkt_stats.rxpkt_recv_cycles[j] = 0;
				pkt_stats.rxpkt_zrecv_cycles[j] = 0;
				pkt_stats.rxpkt_process_cycles[j] = 0;
				pkt_stats.rxpkt_buf_free[j] = 0;
				pkt_stats.rxpkt_dropped[j] = 0;

				/* Restart a new cycle */
				start_tsc = rte_rdtsc();
			}
		}
		exp_start = rte_rdtsc();
		dur = 0;
	} while (1);
	return 0;
}
TESTSUITE_REGISTER_ARGS(test_dpdk, test_udpecho, "start_rxonly","end_rxonly");
