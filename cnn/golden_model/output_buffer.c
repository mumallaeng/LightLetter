#include <string.h>
#include "output_buffer.h"

const char *ob_state_name(ob_state_t s)
{
    static const char *name[] = {"IDLE", "ACCUM_G0", "ACCUM_G1"};
    return name[s];
}

void output_buffer_init(output_buffer_t *m, const ob_param_t *p,
                        const int32_t *bias, uint8_t bias_depth)
{
    memset(m, 0, sizeof(*m));
    m->p = *p;
    bias_rom_load(&m->u_bias_rom, bias, bias_depth);
    output_buffer_reset(m);
}

void output_buffer_reset(output_buffer_t *m)
{
    m->state      = OB_IDLE;  m->state_next      = OB_IDLE;
    m->out_ch_cnt = 0;        m->out_ch_cnt_next = 0;
    m->pixel_cnt  = 0;        m->pixel_cnt_next  = 0;
    m->group_cnt  = 0;        m->group_cnt_next  = 0;
    m->buf_addr   = 0;        m->buf_addr_next   = 0;
    m->sum_data   = 0;        m->sum_data_next   = 0;
    m->sum_valid  = 0;        m->sum_valid_next  = 0;

    m->w_dbg_ch_ovf    = 0;   m->w_dbg_acc_ovf   = 0;
    m->dbg_ch_ovf_cnt  = 0;   m->dbg_acc_ovf_cnt = 0;

    if (m->p.num_groups > 1)
        buffer_ctrl_reset(&m->u_buffer_ctrl);
}

/* always @(*) */
void output_buffer_comb(output_buffer_t *m, const output_buffer_in_t *in,
                        output_buffer_out_t *out)
{
    uint8_t mac_fire = in->mac_valid && (m->state != OB_IDLE);

    uint8_t first_phase = (m->group_cnt == 0);
    uint8_t last_phase  = (m->group_cnt == m->p.num_groups - 1);

    uint8_t ch_last    = (m->out_ch_cnt == m->p.c_out - 1);
    uint8_t pixel_last = (m->pixel_cnt  == m->p.n - 1);
    uint8_t pass_last  = ch_last && pixel_last;

    // ========== Next State / Counter Logic ==========
    m->state_next      = m->state;
    m->out_ch_cnt_next = m->out_ch_cnt;
    m->pixel_cnt_next  = m->pixel_cnt;
    m->group_cnt_next  = m->group_cnt;
    m->buf_addr_next   = m->buf_addr;

    if (m->state == OB_IDLE)
    {
        m->state_next = OB_ACCUM_G0;
    }
    else if (mac_fire)
    {
        m->out_ch_cnt_next = ch_last   ? 0 : m->out_ch_cnt + 1;
        m->buf_addr_next   = pass_last ? 0 : m->buf_addr + 1;

        if (ch_last)
            m->pixel_cnt_next = pixel_last ? 0 : m->pixel_cnt + 1;

        if (pass_last)
        {
            m->group_cnt_next = last_phase ? 0           : m->group_cnt + 1;
            m->state_next     = last_phase ? OB_ACCUM_G0 : OB_ACCUM_G1;
        }
    }

    // ========== Submodule ==========
    bias_rom_in_t  rom_in;
    bias_rom_out_t rom_out;
    rom_in.addr = m->out_ch_cnt;
    bias_rom_comb(&m->u_bias_rom, &rom_in, &rom_out);

    /* sync read: feed the next address so rdata matches the current buf_addr */
    buffer_ctrl_in_t  buf_in;
    buffer_ctrl_out_t buf_out;
    buf_in.raddr  = m->buf_addr_next;
    buf_in.waddr  = m->buf_addr;
    buf_in.wdata  = 0;
    buf_in.we     = 0;
    buf_out.rdata = 0;
    if (m->p.num_groups > 1)
        buffer_ctrl_comb(&m->u_buffer_ctrl, &buf_in, &buf_out);

    partial_sum_in_t  ps_in;
    partial_sum_out_t ps_out;
    ps_in.ch_result0  = in->ch_result0;
    ps_in.ch_result1  = in->ch_result1;
    ps_in.ch_result2  = in->ch_result2;
    ps_in.mac_valid   = mac_fire;
    ps_in.first_phase = first_phase;
    ps_in.last_phase  = last_phase;
    ps_in.buf_rdata   = buf_out.rdata;
    ps_in.bias_rdata  = rom_out.rdata;
    partial_sum_comb(&ps_in, &ps_out);

    /* second pass: connect wdata / we once rdata is known (plain wires in RTL) */
    if (m->p.num_groups > 1)
    {
        buf_in.wdata = ps_out.sum;
        buf_in.we    = ps_out.we && !last_phase;
        buffer_ctrl_comb(&m->u_buffer_ctrl, &buf_in, &buf_out);
    }

    // ========== Output Logic ==========
    out->ch3_5_en  = (m->group_cnt != 0);
    out->sum_data  = m->sum_data;
    out->sum_valid = m->sum_valid;

    m->sum_data_next  = ps_out.sum_data;
    m->sum_valid_next = ps_out.sum_valid;

    m->w_dbg_ch_ovf  = mac_fire && ps_out.dbg_ch_ovf;
    m->w_dbg_acc_ovf = mac_fire && ps_out.dbg_acc_ovf;
}

/* always @(posedge clk) */
void output_buffer_seq(output_buffer_t *m)
{
    m->state      = m->state_next;
    m->out_ch_cnt = m->out_ch_cnt_next;
    m->pixel_cnt  = m->pixel_cnt_next;
    m->group_cnt  = m->group_cnt_next;
    m->buf_addr   = m->buf_addr_next;
    m->sum_data   = m->sum_data_next;
    m->sum_valid  = m->sum_valid_next;

    if (m->p.num_groups > 1)
        buffer_ctrl_seq(&m->u_buffer_ctrl);

    m->dbg_ch_ovf_cnt  += m->w_dbg_ch_ovf;
    m->dbg_acc_ovf_cnt += m->w_dbg_acc_ovf;
}
