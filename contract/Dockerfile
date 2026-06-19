# Pinned, reproducible environment for generating the parity contract.
# The R version is part of the contract's determinism guarantee: regenerating
# in this exact image reproduces the golden values byte-for-byte.
FROM r-base:4.6.0

RUN apt-get update && apt-get install -y \
    libcurl4-openssl-dev \
    libssl-dev \
    libxml2-dev \
    libfontconfig1-dev \
    libharfbuzz-dev \
    libfribidi-dev \
    libfreetype6-dev \
    libpng-dev \
    libtiff5-dev \
    libjpeg-dev \
    make \
    && rm -rf /var/lib/apt/lists/*

# bw's NAMESPACE imports ggplot2/gridExtra/reshape2/survey at load time, so all
# are required even though generate.R only exercises the numeric core.
RUN Rscript -e "\
  pkgs <- c('Rcpp','ggplot2','gridExtra','reshape2','survey','jsonlite'); \
  install.packages(pkgs, repos='https://cloud.r-project.org', Ncpus=4L); \
  missing <- pkgs[!sapply(pkgs, requireNamespace, quietly = TRUE)]; \
  if (length(missing)) stop('Failed to install: ', paste(missing, collapse=', ')) \
"

WORKDIR /pkg
COPY . .

# Pin FMA contraction OFF so the golden values are reproducible. Fused
# multiply-add (a*b+c in one rounding) is compiler/architecture-dependent; left
# on, the same source yields different last-bit values across toolchains, making
# the contract unreproducible. With it off, every same-flag build — including the
# C++-kernel refactor — reproduces the golden bit-for-bit. (NumPy doesn't fuse
# either, so this is also closer to a cross-language port's arithmetic.)
RUN printf 'PKG_CXXFLAGS = -ffp-contract=off\n' > src/Makevars

RUN R CMD INSTALL .

# generate.sh mounts contract/ at /work (live cases.json in, golden/ out).
CMD ["Rscript", "/work/generate.R"]
