#include <linux/module.h>
#include <linux/random.h>
#include <net/tcp.h>

#define THR_SCALE 24
#define THR_UNIT (1 << THR_SCALE)

const char* prefix = "[Astraea-rtcp]";

#define BW_SCALE 24
#define BW_UNIT (1 << BW_SCALE)

#define BBR_SCALE 8	/* scaling factor for fractions in BBR (e.g. gains) */
#define BBR_UNIT (1 << BBR_SCALE)

#define BASED_SCALE 8
#define BASED_UNIT (1 << BASED_SCALE)
// static const u8 percent_arr_num = 13;
// static const int percent_arr[] = {BW_UNIT,BW_UNIT*11/12,BW_UNIT*10/12,BW_UNIT*9/12,BW_UNIT*8/12,BW_UNIT*7/12,BW_UNIT*6/12,BW_UNIT*5/12,BW_UNIT*4/12,BW_UNIT*3/12,BW_UNIT*2/12,BW_UNIT*1/12,0};
static const u8 percent_arr_num = 9;
static const int percent_arr[] = {BW_UNIT,BW_UNIT*7/8,BW_UNIT*6/8,BW_UNIT*5/8,BW_UNIT*4/8,BW_UNIT*3/8,BW_UNIT*2/8,BW_UNIT*1/8,0};
/* If lost/delivered ratio > 20*/
static const u32 loss_thresh = 50;
/* If goodput diff / before empty > 40*/
static const u32 abrupt_decrease_thresh = 150;
static int probe_interval = 20;
static int probe_per = 24;
static int optimize_flag = 1;
static int high_loss_disclassify = 2;
static int monitor_peroid = 3;
static int use_goodput = 1;
static int exclude_RTO = 0;
static int exclude_rwnd = 0;
static int exclude_applimited = 0;
static int enable_printk = 1;

#define MAX_STR_LEN 5000
#define STORE_INTERVAL 400

struct PMODRL {
	u64   B_arr[9];
	u64   R_arr[9];
	u8 best_index;
	u8 classify;
	u32 classify_time_us;
	u8 high_loss_flag;
	u32 loss_start_time_us;
	u32 before_loss_delivered;
	u32 before_loss_time_us;
	u32 before_loss_lost;
	u32 bbr_start_us;
	u64 bef_empty_goodput;
	u32 nominator;

	u32 latest_ack_us;
	u32 lastest_ack_loss;
	u64 detected_bytes_acked;
	u32 detected_time;

	u8 disable_flag;

	u64 mem_B;
	u64 mem_R;

	u8 probe_rtt_flag;

	u8 upper_bound;
	u32 round_count;
	u32 round_count_no;
	u32 next_rtt_delivered;
	u8 round_start;

	u32 transfer_start_deliverd;
	u32 transfer_start_lost;

	char* buffer;
	u32 store_interval;

	u64 acc_rto_dur;

	u64 min_rtt_us;

	u64	cycle_mstamp;	     /* time of this cycle phase start */

	u64 dis_loss_start;
	u64 dis_deliver_start;
	u8 dis_enable_flag;
};


struct astraea {
  /* CA state on previous ACK */
  u32 prev_ca_state : 3;
  /* prior cwnd upon entering loss recovery */
  u32 prior_cwnd;
  struct PMODRL* pmodrl;
};

static void astraea_init(struct sock* sk) {
  // struct tcp_sock* tp = tcp_sk(sk);
  struct astraea* bbr = inet_csk_ca(sk);
  bbr->prev_ca_state = TCP_CA_Open;
  bbr->prior_cwnd = 0;

  cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);

	bbr->pmodrl = kmalloc(sizeof(struct PMODRL), GFP_KERNEL);
	if (bbr->pmodrl){
		memset(bbr->pmodrl,0, sizeof(struct PMODRL));
		bbr->pmodrl->bbr_start_us = jiffies_to_usecs(tcp_jiffies32);

	    bbr->pmodrl->buffer = (char*)kmalloc(MAX_STR_LEN, GFP_KERNEL);
	    if(bbr->pmodrl->buffer) {
	    	memset(bbr->pmodrl->buffer, 0, MAX_STR_LEN);
	    }
	}

}

/* Initialize cwnd to support current pacing rate (but not less then 4 packets)
 */
static void astraea_set_cwnd(struct sock* sk) {
  // struct tcp_sock* tp = tcp_sk(sk);
}

static void astraea_save_cwnd(struct sock* sk) {
  struct tcp_sock* tp = tcp_sk(sk);
  struct astraea* astraea = inet_csk_ca(sk);

  if (astraea->prev_ca_state < TCP_CA_Recovery)
    astraea->prior_cwnd = tp->snd_cwnd; /* this cwnd is good enough */
  else /* loss recovery or BBR_PROBE_RTT have temporarily cut cwnd */
    astraea->prior_cwnd = max(astraea->prior_cwnd, tp->snd_cwnd);
}

static void astraea_update_pacing_rate(struct sock* sk) {
  const struct tcp_sock* tp = tcp_sk(sk);
  u64 rate;
  cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);

  rate = tcp_mss_to_mtu(sk, tcp_sk(sk)->mss_cache);  //

  rate *= USEC_PER_SEC;

  rate *= max(tp->snd_cwnd, tp->packets_out);

  if (likely(tp->srtt_us >> 3)) do_div(rate, tp->srtt_us >> 3);

  /* WRITE_ONCE() is needed because sch_fq fetches sk_pacing_rate
   * without any lock. We want to make sure compiler wont store
   * intermediate values in this location.
   */
  WRITE_ONCE(sk->sk_pacing_rate, min_t(u64, rate, sk->sk_max_pacing_rate));
}

void astraea_update_cwnd(struct sock* sk) {
  struct tcp_sock* tp = tcp_sk(sk);
  tp->snd_cwnd = max(tp->snd_cwnd, tp->cwnd_min);
  tp->snd_cwnd = min(tp->snd_cwnd, tp->snd_cwnd_clamp);
  if (tp->deepcc_enable > 1) astraea_update_pacing_rate(sk);
}

static const int bbr_pacing_margin_percent = 1;

static u64 bbr_rate_bytes_per_sec(struct sock *sk, u64 rate, int gain)
{
	unsigned int mss = tcp_sk(sk)->mss_cache;

	rate *= mss;
	rate *= gain;
	rate >>= BBR_SCALE;
	rate *= USEC_PER_SEC / 100 * (100 - bbr_pacing_margin_percent);
	return rate >> BW_SCALE;
}

static unsigned long bbr_bw_to_pacing_rate_pmodrl(struct sock *sk, u32 bw, int gain, int nominator)
{
	// struct tcp_sock *tp = tcp_sk(sk);
	struct astraea *bbr = inet_csk_ca(sk);
	u64 rate = bw;

	if(bbr->pmodrl && bbr->pmodrl->classify == 1 && nominator != 0){
		gain = gain * probe_per / 20;
	}
	rate = bbr_rate_bytes_per_sec(sk, rate, gain);
	rate = min_t(u64, rate, sk->sk_max_pacing_rate);
	return rate;
}

/**
 * Astraea actually does not need this function as we don't always
 * want to reduce CWND in losses.
 */
static u32 astraea_undo_cwnd(struct sock* sk) { return tcp_sk(sk)->snd_cwnd; }

/* save current cwnd for quick ramp up */
static u32 astraea_ssthresh(struct sock* sk) {
  const struct tcp_sock* tp = tcp_sk(sk);
  // we want RL to take more efficient control
  astraea_save_cwnd(sk);
  return max(tp->snd_cwnd, 10U);
}

static void astraea_cong_avoid(struct sock* sk, u32 ack, u32 acked) {
  // printk(KERN_INFO "[TCP Astraea] Nothing done in Astraea CC");
  // struct tcp_sock* tp = tcp_sk(sk);
  // if (tcp_in_cwnd_reduction(sk)) {
  //   // prior_cwnd is the cwnd right before starting loss recovery
  //   if (tp->prior_cwnd > tp->snd_cwnd) tp->snd_cwnd = tp->prior_cwnd;
  // }
}

static int comp(struct sock *sk, u32 now_us){
	struct astraea *bbr = inet_csk_ca(sk);
	u8 best_index = 0;
	u64 b_diff;
	u64 r_diff;
	u64 flow_len_us;
	u8 i;
	for(i = 1; i < percent_arr_num; i++){
		b_diff = (u64)abs(bbr->pmodrl->B_arr[i] - bbr->pmodrl->B_arr[best_index]);
		r_diff = (u64)abs(bbr->pmodrl->R_arr[i] - bbr->pmodrl->R_arr[best_index]);
		flow_len_us = now_us - bbr->pmodrl->bbr_start_us;
		if(r_diff == 0){
			best_index = i;
		}
		else{
			if(div_u64(b_diff * BASED_SCALE * 2, r_diff) > flow_len_us * BASED_SCALE){
				best_index = i;
			}
			else{
				break;
			}	
		}
	}
	return best_index;
}

static void estimation_classify(struct sock *sk){
	struct tcp_sock *tp = tcp_sk(sk);
	struct astraea *bbr = inet_csk_ca(sk);
	u32 now_us = jiffies_to_usecs(tcp_jiffies32);
	u32 cur_delivered = tp->delivered - bbr->pmodrl->transfer_start_deliverd;
	u32 cur_lost = tp->lost - bbr->pmodrl->transfer_start_lost;
	u32 d;
	u32 l;
	u64 bef_empty;
	u8 i;
	u64 h;
	u64 t;
	u64 R;
	u64 incr_diff;
	u8 abrupt_decrease_flag = 0;
	u8 best_index = 0;
	u64 lower_bound_B;

	if(use_goodput){
		cur_delivered = tp->snd_una / tp->mss_cache - bbr->pmodrl->transfer_start_deliverd;
	}

	if(bbr->pmodrl->high_loss_flag == 0){
		if(bbr->pmodrl->loss_start_time_us != 0 && bbr->pmodrl->loss_start_time_us + 7 * bbr->pmodrl->min_rtt_us < now_us){
			d = cur_delivered - bbr->pmodrl->before_loss_delivered;
			l = cur_lost - bbr->pmodrl->before_loss_lost;
			// if(d < 10) {
			// 	return;
			// }
			if((d + l) != 0 && (u64)l * 10 > (u64)(d + l) * 2){
				bbr->pmodrl->high_loss_flag = 1;
				t = div_u64(bbr->pmodrl->before_loss_time_us, USEC_PER_MSEC) - div_u64(bbr->pmodrl->bbr_start_us, USEC_PER_MSEC);
				if ((s32)t < 1){
					return;	
				}
				bef_empty = div_u64((u64)bbr->pmodrl->before_loss_delivered * BW_UNIT, bbr->pmodrl->before_loss_time_us - bbr->pmodrl->bbr_start_us);
				bbr->pmodrl->bef_empty_goodput = bef_empty;
				lower_bound_B = (u64)bbr->pmodrl->before_loss_delivered * (BASED_UNIT -  abrupt_decrease_thresh);
				for(i = 0; i < percent_arr_num; i++){
					if(percent_arr[i] == 0){
						bbr->pmodrl->B_arr[i] = 0;
					}
					else{
						t = (BW_UNIT - percent_arr[i]) * lower_bound_B;
						t = t >> BASED_SCALE;
						bbr->pmodrl->B_arr[i] = (u64)bbr->pmodrl->before_loss_delivered * percent_arr[i] + t;
					}
				}
				for(i = 0; i < percent_arr_num; i++){
					if((u64)bbr->pmodrl->before_loss_delivered * BW_UNIT > bbr->pmodrl->B_arr[i]){
						h = (u64)bbr->pmodrl->before_loss_delivered * BW_UNIT - bbr->pmodrl->B_arr[i];
						t = div_u64(bbr->pmodrl->before_loss_time_us, USEC_PER_MSEC) - div_u64(bbr->pmodrl->bbr_start_us, USEC_PER_MSEC);
						if ((s32)t < 1){
							return;	
						}
						R = div_u64(h, bbr->pmodrl->before_loss_time_us - bbr->pmodrl->bbr_start_us);
						bbr->pmodrl->R_arr[i] = max(bbr->pmodrl->R_arr[i], R);
					}
				}
			}
			else{
				bbr->pmodrl->loss_start_time_us = 0;
				return;
			}
		}
		else{
			return;
		}
	}
	for(i = 0; i < percent_arr_num; i++){
		if((u64)cur_delivered * BW_UNIT > bbr->pmodrl->B_arr[i]){
			h = (u64)cur_delivered * BW_UNIT - bbr->pmodrl->B_arr[i];
			t = div_u64(now_us, USEC_PER_MSEC) - div_u64(bbr->pmodrl->bbr_start_us, USEC_PER_MSEC);
			if ((s32)t < 1){
				return;	
			}
			R = div_u64(h, now_us - bbr->pmodrl->bbr_start_us);
			bbr->pmodrl->R_arr[i] = max(bbr->pmodrl->R_arr[i], R);
		}
	}
	best_index = comp(sk, now_us);
	bbr->pmodrl->best_index = best_index;
	while(best_index == 0){
		incr_diff = bbr->pmodrl->B_arr[0] - bbr->pmodrl->B_arr[1];
		for(i = percent_arr_num - 1; i>=1; i--){
			bbr->pmodrl->B_arr[i] = bbr->pmodrl->B_arr[i - 1];
			bbr->pmodrl->R_arr[i] = bbr->pmodrl->R_arr[i - 1];
		}
		bbr->pmodrl->B_arr[0] = bbr->pmodrl->B_arr[0] + incr_diff;
		bbr->pmodrl->R_arr[0] = 0;
		if((u64)cur_delivered * BW_UNIT > bbr->pmodrl->B_arr[0]){
			h = (u64)cur_delivered * BW_UNIT - bbr->pmodrl->B_arr[0];
			R = div_u64(h, now_us - bbr->pmodrl->bbr_start_us);
			bbr->pmodrl->R_arr[i] = max(bbr->pmodrl->R_arr[i], R);	
		}
		if((u64)bbr->pmodrl->before_loss_delivered * BW_UNIT > bbr->pmodrl->B_arr[0]){
			h = (u64)bbr->pmodrl->before_loss_delivered * BW_UNIT - bbr->pmodrl->B_arr[0];
			R = div_u64(h, bbr->pmodrl->before_loss_time_us - bbr->pmodrl->bbr_start_us);
			bbr->pmodrl->R_arr[i] = max(bbr->pmodrl->R_arr[i], R);			
		}
		best_index = comp(sk, now_us);
	}
	bbr->pmodrl->best_index = best_index;
	if(bbr->pmodrl->R_arr[best_index] * BASED_UNIT <= abrupt_decrease_thresh * bbr->pmodrl->bef_empty_goodput){
		abrupt_decrease_flag = 1;
	}
	if(bbr->pmodrl->classify == 1){
		if(!abrupt_decrease_flag){
			// printA(KERN_INFO "!!!Rate fail %llu", bbr->pmodrl->R_arr[best_index]);
			// u64 cycle_mstamp = bbr->pmodrl->cycle_mstamp;
			// memset(bbr->pmodrl, 0, sizeof(struct PMODRL));
			bbr->pmodrl->classify = 2;
			bbr->pmodrl->disable_flag = 1;
			// bbr->pmodrl->cycle_mstamp = cycle_mstamp;
		}
	}
	else{
		// printk(KERN_INFO "!!!%u %u %llu %llu %u %u", bbr->pmodrl->high_loss_flag, abrupt_decrease_flag, bbr->pmodrl->R_arr[best_index], bbr->pmodrl->bef_empty_goodput, now_us, bbr->pmodrl->classify_time_us);
		if(bbr->pmodrl->high_loss_flag && abrupt_decrease_flag){
			if(bbr->pmodrl->classify_time_us == 0){
				bbr->pmodrl->classify_time_us = now_us;
			}
			
			if(bbr->pmodrl->R_arr[best_index] != bbr->pmodrl->mem_R || bbr->pmodrl->B_arr[best_index] != bbr->pmodrl->mem_B) {
				bbr->pmodrl->classify_time_us = now_us;
				bbr->pmodrl->mem_B = bbr->pmodrl->B_arr[best_index];
				bbr->pmodrl->mem_R = bbr->pmodrl->R_arr[best_index];

			}
			else{
				if(now_us - bbr->pmodrl->classify_time_us > 10 * bbr->pmodrl->min_rtt_us){
					bbr->pmodrl->classify = 1;
					bbr->pmodrl->upper_bound = 1;
					bbr->pmodrl->detected_time = now_us - bbr->pmodrl->bbr_start_us;
					bbr->pmodrl->detected_bytes_acked = tp->bytes_acked;
				}
			}

		}
		else{
			bbr->pmodrl->classify_time_us = 0;
		}
	}

}

static void probe_pmodrl(struct sock *sk) {
	struct astraea *bbr = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	if(bbr->pmodrl) {
		if(bbr->pmodrl->classify == 1 && optimize_flag){
			if(bbr->pmodrl->upper_bound != 1 || bbr->pmodrl->nominator != 0) {
				if(bbr->pmodrl->round_start){
					bbr->pmodrl->round_count_no++;
					if(bbr->pmodrl->round_count_no >= monitor_peroid && bbr->pmodrl->mem_B == bbr->pmodrl->B_arr[bbr->pmodrl->best_index] && bbr->pmodrl->mem_R == bbr->pmodrl->R_arr[bbr->pmodrl->best_index]){
						bbr->pmodrl->upper_bound = 1;
						bbr->pmodrl->nominator = 0;
						bbr->pmodrl->round_count_no = 0;
					}
				}
				if(bbr->pmodrl->mem_B != bbr->pmodrl->B_arr[bbr->pmodrl->best_index] || bbr->pmodrl->mem_R != bbr->pmodrl->R_arr[bbr->pmodrl->best_index]){
					bbr->pmodrl->upper_bound = 2;
					bbr->pmodrl->nominator = 0;
					bbr->pmodrl->mem_B = bbr->pmodrl->B_arr[bbr->pmodrl->best_index];
					bbr->pmodrl->mem_R = bbr->pmodrl->R_arr[bbr->pmodrl->best_index];
					bbr->pmodrl->round_count_no = 0;
					bbr->pmodrl->next_rtt_delivered = tp->delivered;

					bbr->pmodrl->dis_loss_start = 2;
				}				
			}
			else{
				if(bbr->pmodrl->round_start) {
					bbr->pmodrl->round_count++;
					if(bbr->pmodrl->round_count >= probe_interval){
						bbr->pmodrl->upper_bound = 1;
						bbr->pmodrl->nominator = 1;
						// bbr->pmodrl->acc_rto_dur = 0;
						bbr->pmodrl->mem_B = bbr->pmodrl->B_arr[bbr->pmodrl->best_index];
						bbr->pmodrl->mem_R = bbr->pmodrl->R_arr[bbr->pmodrl->best_index];
						bbr->pmodrl->round_count = 0;
						bbr->pmodrl->round_count_no = 0;
					}
				}
			}

		}
	}
}

static void reset_pmodrl(struct sock *sk, u8 res1, u8 res2){
	struct astraea *bbr = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	char* p;
	int flag = 0;
	if(bbr->pmodrl->classify == 1){
		flag = 1;
	}
	else if(bbr->pmodrl->classify == 2){
		flag = 2;
	}
	else if(bbr->pmodrl->classify != 0){
		flag = bbr->pmodrl->classify;
	}
	p = bbr->pmodrl->buffer;
	memset(bbr->pmodrl,0, sizeof(struct PMODRL));
	bbr->pmodrl->bbr_start_us = jiffies_to_usecs(tcp_jiffies32);
	bbr->pmodrl->transfer_start_lost = tp->lost;
	if(use_goodput){
		bbr->pmodrl->transfer_start_deliverd = tp->snd_una / tp->mss_cache;
	}
	else{
		bbr->pmodrl->transfer_start_deliverd = tp->delivered;
	}
	bbr->pmodrl->buffer = p;
	if(flag == 1){
		bbr->pmodrl->classify = res1;
	}
	else if(flag == 2){
		bbr->pmodrl->classify = res2;
	}
	else if(flag != 0){
		bbr->pmodrl->classify = flag;
	}
}

/**
 * @brief We implement this function to bypass the CWND reduction in loss &
 * recovery state Note that: Updating of CWND and pacing rate should be done
 * here as this function will end utilimate congestion control logic.
 *
 * @param sk pointer to current socket
 * @param rs pointer to rate sample
 */
static void astraea_cong_control(struct sock* sk,
                                 const struct rate_sample* rs) {
  struct tcp_sock* tp = tcp_sk(sk);
  struct astraea* bbr = inet_csk_ca(sk);
  u32 now_us = jiffies_to_usecs(tcp_jiffies32);
  struct inet_sock *inet = inet_sk(sk);
  // print rate sample
  u64 bw;
  u64 srtt = tp->srtt_us >> 3;

  // we believe cwnd has been modified by user-space RL-agent
  u32 cwnd = max(tp->prior_cwnd, bbr->prior_cwnd);
  tp->snd_cwnd = max(tp->snd_cwnd, cwnd);
  astraea_update_cwnd(sk);

  if (rs->delivered < 0 || rs->interval_us <= 0) {
    bw = 0;
  } else {
    // we first need to enlarge bw thus avoiding get zero
    bw = (u64)rs->delivered * THR_UNIT;
    do_div(bw, rs->interval_us);
    // deliverd is num of packers, we translate it to bytes, bw in bytes per
    // second
    bw = bw * tp->mss_cache * USEC_PER_SEC >> THR_SCALE;
  }

  if(bbr->pmodrl){
  	if(bbr->pmodrl->min_rtt_us == 0){
  		bbr->pmodrl->min_rtt_us = rs->rtt_us;
  	}
  	bbr->pmodrl->min_rtt_us = min((u32)bbr->pmodrl->min_rtt_us, rs->rtt_us);
  }

	if(bbr->pmodrl){
		bbr->pmodrl->latest_ack_us = now_us;

		if(bbr->pmodrl->bbr_start_us == 0){
			bbr->pmodrl->bbr_start_us = now_us;
		}
		if(bbr->pmodrl->disable_flag == 0){
			estimation_classify(sk);
		}

		if(bbr->pmodrl->lastest_ack_loss!=tp->lost){
			if(bbr->pmodrl->high_loss_flag == 0 && bbr->pmodrl->loss_start_time_us == 0){
				bbr->pmodrl->loss_start_time_us = now_us;
			}
		}
		else{
			if(bbr->pmodrl->high_loss_flag == 0 && bbr->pmodrl->loss_start_time_us == 0) {
				bbr->pmodrl->before_loss_delivered = tp->delivered - bbr->pmodrl->transfer_start_deliverd;
				bbr->pmodrl->before_loss_time_us = now_us;
				bbr->pmodrl->before_loss_lost = tp->lost - bbr->pmodrl->transfer_start_lost;
				if(use_goodput){
					bbr->pmodrl->before_loss_delivered = tp->snd_una / tp->mss_cache - bbr->pmodrl->transfer_start_deliverd;
				}
			}
		}
		bbr->pmodrl->lastest_ack_loss = tp->lost;

		if(tp->write_seq - tp->snd_nxt < tp->mss_cache && sk_wmem_alloc_get(sk) < SKB_TRUESIZE(1) && tcp_packets_in_flight(tp) < tp->snd_cwnd && tp->lost_out <= tp->retrans_out){
			bbr->pmodrl->probe_rtt_flag = 0;
		}

		bbr->pmodrl->round_start = 0;
		if (!before(rs->prior_delivered, bbr->pmodrl->next_rtt_delivered) && !(rs->delivered < 0 || rs->interval_us <= 0)) {
			bbr->pmodrl->next_rtt_delivered = tp->delivered;
			bbr->pmodrl->round_start = 1;
		}

		probe_pmodrl(sk);
	}

  if(bbr->pmodrl){
		bbr->pmodrl->store_interval+=1;
		if(bbr->pmodrl->buffer && bbr->pmodrl->store_interval >= STORE_INTERVAL){
			bbr->pmodrl->store_interval = 0;
			if(strlen(bbr->pmodrl->buffer) + 90 < MAX_STR_LEN){
				char temp[90];
				memset(temp, 0, 90);
				snprintf(temp, sizeof(temp), "%llu;%u;%llu;%llu-", tp->bytes_acked, bbr->pmodrl->classify, bbr->pmodrl->B_arr[bbr->pmodrl->best_index], bbr->pmodrl->R_arr[bbr->pmodrl->best_index]);
				strcat(bbr->pmodrl->buffer, temp);
			}
		}
		if(exclude_rwnd && tp->chrono_type == TCP_CHRONO_RWND_LIMITED){
			reset_pmodrl(sk, (u8)5, (u8)6);
		}

		if(exclude_RTO && bbr->prev_ca_state == TCP_CA_Loss && inet_csk(sk)->icsk_ca_state != TCP_CA_Loss){
			reset_pmodrl(sk, (u8)7, (u8)8);
		}

		if(exclude_applimited && rs->is_app_limited){
			reset_pmodrl(sk, (u8)9, (u8)10);
		}
		if(enable_printk){
			printk(KERN_INFO "!!!ACK: ip:%pI4 port:%hu c:%u B:%llu R:%llu n:%u u_p:%lu r_p:%lu b:%llu d:%u l:%u rd:%u rl:%u u:%u rc:%u rcn:%u cl:%u def:%u cwnd:%u adv:%u inflight:%u s:%llu", 
				&sk->sk_daddr, ntohs(inet->inet_dport), bbr->pmodrl->classify, bbr->pmodrl->B_arr[bbr->pmodrl->best_index], bbr->pmodrl->R_arr[bbr->pmodrl->best_index], 
				bbr->pmodrl->nominator, bbr_bw_to_pacing_rate_pmodrl(sk,bbr->pmodrl->R_arr[bbr->pmodrl->best_index],BBR_UNIT,bbr->pmodrl->nominator), sk->sk_pacing_rate, tp->bytes_acked, tp->delivered, tp->lost, 
				rs->delivered, rs->losses ,bbr->pmodrl->upper_bound, bbr->pmodrl->round_count, bbr->pmodrl->round_count_no, tcp_is_cwnd_limited(sk), bbr->pmodrl->dis_enable_flag, tp->snd_cwnd, tp->rcv_wnd,tcp_packets_in_flight(tp),
				tp->bytes_sent);	
		}	
	}

	if(bbr->pmodrl && bbr->pmodrl->classify == 1 && bbr->pmodrl->upper_bound == 1){
		unsigned long pmodrl_rate = bbr_bw_to_pacing_rate_pmodrl(sk, bbr->pmodrl->R_arr[bbr->pmodrl->best_index], BBR_UNIT, bbr->pmodrl->nominator);
		// printA(KERN_INFO "!!! rate:%llu  pmodrl_rate:%llu\n",rate, pmodrl_rate);
		if(sk->sk_pacing_rate > pmodrl_rate && optimize_flag){
			sk->sk_pacing_rate = pmodrl_rate;
		}
	}
  if(bbr->pmodrl && bbr->pmodrl->classify == 1 && bbr->pmodrl->upper_bound == 1 && optimize_flag){
    u64 temp = ca->pmodrl->R_arr[ca->pmodrl->best_index] * srtt;
    u32 upper_bound;
		temp = temp >> BW_SCALE;
		upper_bound = temp + 1;
		if(ca->pmodrl->nominator != 0){
			multiplier = BASED_UNIT;
			multiplier = multiplier * probe_per / 20;
			temp = upper_bound * multiplier;
			temp = temp >> BASED_SCALE;
			temp = temp + 1;
			upper_bound = temp;
		}
		if(tp->snd_cwnd > upper_bound){
			tp->snd_cwnd = upper_bound;
		}
  }

  // printk(KERN_INFO
  //        "%s: snd_cwnd: %u, rcv_wnd: %u, current_state: %u, pacing_rate: %lu, "
  //        "sampled_rate: %llu, deliverd: %u, interval_us:%lu, packet_out:%u",
  //        prefix, tp->snd_cwnd, tp->rcv_wnd, inet_csk(sk)->icsk_ca_state,
  //        sk->sk_pacing_rate, bw, rs->delivered, rs->interval_us,
  //        tp->packets_out);
}

static void astraea_pkts_acked(struct sock* sk, const struct ack_sample* acks) {
  struct tcp_sock* tp = tcp_sk(sk);
  s32 rtt = max(acks->rtt_us, 0);
	struct astraea *bbr = inet_csk_ca(sk);
	u32 now_us = jiffies_to_usecs(tcp_jiffies32);
	u64 srtt;
	srtt = tp->srtt_us >> 3;
  // printk(KERN_INFO "%s: cwnd: %u, current_state: %u, sampled_rtt: %u", prefix,
  //        tp->snd_cwnd, inet_csk(sk)->icsk_ca_state, rtt);
  
}

static void astraea_ack_event(struct sock* sk, u32 flags) {}

static void astraea_cwnd_event(struct sock* sk, enum tcp_ca_event event) {
	struct tcp_sock* tp = tcp_sk(sk);
  u32 now_us = jiffies_to_usecs(tcp_jiffies32);
  if (event == CA_EVENT_LOSS) {
    printk(KERN_INFO "%s packet loss: cwnd: %u, current_state: %u", prefix,
           tcp_sk(sk)->snd_cwnd, inet_csk(sk)->icsk_ca_state);
  }
  if (event == CA_EVENT_TX_START && tp->app_limited) {
    struct astraea *bbr = inet_csk_ca(sk);
		bbr->pmodrl->bbr_start_us = now_us;
		bbr->pmodrl->transfer_start_lost = tp->lost;
		bbr->pmodrl->transfer_start_deliverd = tp->delivered;
		if(use_goodput){
			bbr->pmodrl->transfer_start_deliverd = tp->snd_una / tp->mss_cache;
		}
  }
}

static void astraea_set_state(struct sock* sk, u8 new_state) {
  struct astraea* astraea = inet_csk_ca(sk);

  if (new_state == TCP_CA_Loss) {
    astraea->prev_ca_state = TCP_CA_Loss;
  } else if (new_state == TCP_CA_Recovery) {
    printk(KERN_INFO "%s recovery: cwnd: %u, current_state: %u", prefix,
           tcp_sk(sk)->snd_cwnd, inet_csk(sk)->icsk_ca_state);
  }
}

static void bbr_release(struct sock *sk){
  struct astraea *bbr = inet_csk_ca(sk);
  if (!bbr->pmodrl)
        return;
  if(bbr->pmodrl->buffer){
    kfree(bbr->pmodrl->buffer);
    bbr->pmodrl->buffer = NULL;
  }
  kfree(bbr->pmodrl);
  bbr->pmodrl = NULL;
}

module_param_named(probe_interval_external, probe_interval, int, 0644);
module_param_named(probe_per_external, probe_per, int, 0644);
module_param_named(optimize_flag_external, optimize_flag, int, 0644);
module_param_named(high_loss_disclassify_external, high_loss_disclassify, int, 0644);
module_param_named(monitor_peroid_external, monitor_peroid, int, 0644);
module_param_named(exclude_RTO_external, exclude_RTO, int, 0644);
module_param_named(exclude_rwnd_external, exclude_rwnd, int, 0644);
module_param_named(use_goodput_external, use_goodput, int, 0644);
module_param_named(exclude_applimited_external, exclude_applimited, int, 0644);
module_param_named(enable_printk_external, enable_printk, int, 0644);

static struct tcp_congestion_ops tcp_astraea_ops __read_mostly = {
    .flags = TCP_CONG_NON_RESTRICTED,
    .name = "rtcp_astraea",
    .owner = THIS_MODULE,
    .init = astraea_init,
    .cong_control = astraea_cong_control,
    .undo_cwnd = astraea_undo_cwnd,
    .ssthresh = astraea_ssthresh,
    .set_state = astraea_set_state,
    .cong_avoid = astraea_cong_avoid,
    .pkts_acked = astraea_pkts_acked,
    // .in_ack_event = astraea_ack_event,
    .cwnd_event = astraea_cwnd_event,
	  .release	= bbr_release
};

/* Kernel module section */
static int __init astraea_register(void) {
  BUILD_BUG_ON(sizeof(struct astraea) > ICSK_CA_PRIV_SIZE);
  printk(KERN_INFO
         "[TCP Astraea] Astraea init clean tcp congestion control logic\n");
  return tcp_register_congestion_control(&tcp_astraea_ops);
}

static void __exit astraea_unregister(void) {
  printk(KERN_INFO "[TCP Astraea] Astraea unregistered");
  tcp_unregister_congestion_control(&tcp_astraea_ops);
}

module_init(astraea_register);
module_exit(astraea_unregister);

MODULE_AUTHOR("Xudong Liao <stephenxudong@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("TCP Astraea (Clean-version TCP Congestion Control)");
