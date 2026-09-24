#include "fc_ctrl.h"

void fc_ctrl_init(fc_ctrl_t *m, const fc_param_t *p)
{
    m->p = *p;
    fc_ctrl_reset(m);
}

void fc_ctrl_reset(fc_ctrl_t *m)
{
    m->state  = FC_IDLE;  m->state_next  = FC_IDLE;
    m->neuron = 0;        m->neuron_next = 0;
    m->chunk  = 0;        m->chunk_next  = 0;
}

/* always @(*) */
void fc_ctrl_comb(fc_ctrl_t *m, const fc_ctrl_in_t *in, fc_ctrl_out_t *out)
{
    uint8_t mac_en      = (m->state == FC_RUN);
    uint8_t neuron_last = (m->neuron + 1 == m->p.n_out);
    uint8_t next_chunk  = (uint8_t)((m->chunk + 1 == m->p.num_chunk) ? 0 : m->chunk + 1);
    uint8_t chunk_done  = mac_en && neuron_last;

    // ========== Next State / Counter Logic ==========
    m->state_next  = m->state;
    m->neuron_next = m->neuron;
    m->chunk_next  = m->chunk;

    if (m->state == FC_IDLE)
    {
        if (in->calc_full)
        {
            m->state_next  = FC_RUN;
            m->neuron_next = 0;
        }
    }
    else if (neuron_last)
    {
        m->neuron_next = 0;
        m->chunk_next  = next_chunk;
        /* keep running when the next chunk is already in the fill buffer */
        m->state_next  = in->next_full ? FC_RUN : FC_IDLE;
    }
    else
    {
        m->neuron_next = (uint8_t)(m->neuron + 1);
    }

    // ========== Output Logic ==========
    /* address of the next mac_en: same chunk and next neuron, or neuron 0 of the next chunk */
    uint8_t addr_chunk  = (mac_en && neuron_last) ? next_chunk : m->chunk;
    uint8_t addr_neuron = (m->state == FC_RUN && !neuron_last) ? (uint8_t)(m->neuron + 1) : 0;

    out->mac_en     = mac_en;
    out->rom_addr   = (uint16_t)((uint16_t)addr_chunk * m->p.n_out + addr_neuron);
    out->chunk_done = chunk_done;
}

/* always @(posedge clk) */
void fc_ctrl_seq(fc_ctrl_t *m)
{
    m->state  = m->state_next;
    m->neuron = m->neuron_next;
    m->chunk  = m->chunk_next;
}

const char *fc_state_name(fc_state_t s)
{
    switch (s)
    {
        case FC_IDLE: return "WAIT";
        case FC_RUN:  return "RUN";
        default:      return "?";
    }
}
