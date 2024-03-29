# SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB)
# Copyright (c) 2019, Mellanox Technologies. All rights reserved. See COPYING file

rdma_cython_module(pyverbs ""
  addr.pyx
  base.pyx
  cm_enums.pyx
  cmid.pyx
  cq.pyx
  device.pyx
  enums.pyx
  mem_alloc.pyx
  mr.pyx
  pd.pyx
  qp.pyx
  wr.pyx
  xrcd.pyx
  srq.pyx
  )

rdma_python_module(pyverbs
  __init__.py
  pyverbs_error.py
  utils.py
  )

# mlx5 and efa providers are not built without coherent DMA, e.g. ARM32 build.
if (HAVE_COHERENT_DMA)
add_subdirectory(providers/mlx5)
if (0)
add_subdirectory(providers/efa)
endif()
endif()
