FROM ornladios/adios2:fedora-sanitizers-base

# Install core dev packages
RUN dnf -y install libasan python3-devel python3-numpy hdf5-devel

# Misc cleanup
RUN dnf clean all && \
    rm -rfv /tmp/* /var/cache/dnf
