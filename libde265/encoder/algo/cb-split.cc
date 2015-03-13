/*
 * H.265 video codec.
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * Authors: struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libde265.
 *
 * libde265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libde265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libde265.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "libde265/encoder/algo/cb-split.h"
#include "libde265/encoder/algo/coding-options.h"
#include "libde265/encoder/encoder-context.h"
#include <assert.h>
#include <limits>
#include <math.h>


#define ENCODER_DEVELOPMENT 1





// Utility function to encode all four children in a splitted CB.
// Children are coded with the specified algo_cb_split.
enc_cb* Algo_CB_Split::encode_cb_split(encoder_context* ectx,
                                       context_model_table& ctxModel,
                                       enc_cb* cb)
{
  int w = ectx->imgdata->input->get_width();
  int h = ectx->imgdata->input->get_height();


  cb->split_cu_flag = true;


  // encode all 4 children and sum their distortions and rates

  for (int i=0;i<4;i++) {
    int child_x = cb->x + ((i&1)  << (cb->log2Size-1));
    int child_y = cb->y + ((i>>1) << (cb->log2Size-1));

    if (child_x>=w || child_y>=h) {
      cb->children[i] = NULL;
    }
    else {
      enc_cb* childCB = new enc_cb;
      childCB->log2Size = cb->log2Size-1;
      childCB->ctDepth  = cb->ctDepth+1;
      childCB->qp = cb->qp;
      childCB->x = child_x;
      childCB->y = child_y;

      cb->children[i] = analyze(ectx, ctxModel, childCB);

      cb->distortion += cb->children[i]->distortion;
      cb->rate       += cb->children[i]->rate;
    }
  }

  return cb;
}




enc_cb* Algo_CB_Split_BruteForce::analyze(encoder_context* ectx,
                                          context_model_table& ctxModel,
                                          enc_cb* cb_input)
{
  assert(cb_input->pcm_flag==0);

  // --- prepare coding options ---

  const SplitType split_type = get_split_type(&ectx->sps,
                                              cb_input->x, cb_input->y,
                                              cb_input->log2Size);


  const bool can_split_CB   = (split_type != ForcedNonSplit);
  const bool can_nosplit_CB = (split_type != ForcedSplit);

  CodingOptions options(ectx, cb_input, ctxModel);

  CodingOption option_split    = options.new_option(can_split_CB);
  CodingOption option_no_split = options.new_option(can_nosplit_CB);

  options.start();

  // --- encode without splitting ---

  if (option_no_split) {
    CodingOption& opt = option_no_split; // abbrev.

    opt.begin();

    enc_cb* cb = opt.get_cb();

    // set CB size in image data-structure
    ectx->img->set_ctDepth(cb->x,cb->y,cb->log2Size, cb->ctDepth);
    ectx->img->set_log2CbSize(cb->x,cb->y,cb->log2Size, true);

    // analyze subtree
    assert(mPredModeAlgo);
    cb = mPredModeAlgo->analyze(ectx, opt.get_context(), cb);

    // add rate for split flag
    encode_split_cu_flag(ectx,opt.get_cabac(), cb->x,cb->y, cb->ctDepth, 0);
    cb->rate += opt.get_cabac_rate();

    opt.set_cb(cb);
    opt.end();
  }

  // --- encode with splitting ---

  if (option_split) {
    option_split.begin();

    enc_cb* cb = option_split.get_cb();
    cb = encode_cb_split(ectx, option_split.get_context(), cb);

    // add rate for split flag
    encode_split_cu_flag(ectx,option_split.get_cabac(), cb->x,cb->y, cb->ctDepth, 1);
    cb->rate += option_split.get_cabac_rate();

    option_split.set_cb(cb);
    option_split.end();
  }


  options.compute_rdo_costs();
  return options.return_best_rdo();
}
