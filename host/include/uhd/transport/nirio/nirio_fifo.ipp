/*
 * fifo_manager.hpp
 *
 *  Created on: Mar 28, 2013
 *      Author: ashish
 */

template <typename data_t>
nirio_fifo<data_t>::nirio_fifo() :
	_datatype_info(_get_datatype_info()),
	_lock(0, 0),
    _riok_proxy_ptr(NULL)
{
}

template <typename data_t>
nirio_fifo<data_t>::nirio_fifo(
    niriok_proxy& riok_proxy,
    fifo_direction_t direction,
	const std::string& name,
    uint32_t fifo_instance) :
    _fifo_channel(fifo_instance),
    _name(name),
    _fifo_direction(direction),
    _datatype_info(_get_datatype_info()),
    _started(false),
    _mem_map(),
	_lock(riok_proxy.get_interface_num(), fifo_instance),
    _riok_proxy_ptr(&riok_proxy)
{
}

template <typename data_t>
nirio_fifo<data_t>::~nirio_fifo()
{
	finalize();
}

template <typename data_t>
nirio_status nirio_fifo<data_t>::initialize(
    const size_t requested_depth,
    size_t& actual_depth,
    size_t& actual_size)
{
	nirio_status status = NiRio_Status_Success;
	if (!_riok_proxy_ptr) return NiRio_Status_ResourceNotInitialized;

	status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_fatal(status)) return status;

	nNIRIOSRV200::tRioDeviceSocketInputParameters in = {};
	nNIRIOSRV200::tRioDeviceSocketOutputParameters out = {};

	in.function = nNIRIOSRV200::nRioFunction::kFifo;
	in.subfunction = nNIRIOSRV200::nRioDeviceFifoFunction::kConfigure;

	in.params.fifo.channel = _fifo_channel;
	in.params.fifo.op.config.requestedDepth = static_cast<uint32_t>(requested_depth);
	in.params.fifo.op.config.requiresActuals = 1;

	status = _riok_proxy_ptr->sync_operation(&in, sizeof(in), &out, sizeof(out));
	if (nirio_status_fatal(status)) return status;

	actual_depth = out.params.fifo.op.config.actualDepth;
	actual_size = out.params.fifo.op.config.actualSize;

	status = _riok_proxy_ptr->map_fifo_memory(_fifo_channel, actual_size, _mem_map);
	_lock.release();
	return status;
}

template <typename data_t>
void nirio_fifo<data_t>::finalize()
{
	nirio_status status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_not_fatal(status)) return;

	if (_started) stop();
	_riok_proxy_ptr->unmap_fifo_memory(_mem_map);

	_lock.release();
}

template <typename data_t>
nirio_status nirio_fifo<data_t>::start()
{
	nirio_status status = NiRio_Status_Success;
	if (!_riok_proxy_ptr) return NiRio_Status_ResourceNotInitialized;

	status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_fatal(status)) return status;

	nNIRIOSRV200::tRioDeviceSocketInputParameters in = {};
    nNIRIOSRV200::tRioDeviceSocketOutputParameters out = {};

    in.function    = nNIRIOSRV200::nRioFunction::kFifo;
    in.subfunction = nNIRIOSRV200::nRioDeviceFifoFunction::kStart;

    status = _riok_proxy_ptr->sync_operation(&in, sizeof(in), &out, sizeof(out));
    _started = nirio_status_not_fatal(status);
	_lock.release();

	return status;
}

template <typename data_t>
nirio_status nirio_fifo<data_t>::stop()
{
	nirio_status status = NiRio_Status_Success;
	if (!_riok_proxy_ptr) return NiRio_Status_ResourceNotInitialized;

	status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_fatal(status)) return status;

	if (!_started) return NiRio_Status_Success;

    nNIRIOSRV200::tRioDeviceSocketInputParameters in = {};
    nNIRIOSRV200::tRioDeviceSocketOutputParameters out = {};

    in.function    = nNIRIOSRV200::nRioFunction::kFifo;
    in.subfunction = nNIRIOSRV200::nRioDeviceFifoFunction::kStop;

    status = _riok_proxy_ptr->sync_operation(&in, sizeof(in), &out, sizeof(out));
	_lock.release();
	return status;
}

template <typename data_t>
nirio_status nirio_fifo<data_t>::acquire(
	data_t*& elements,
	const size_t elements_requested,
	const uint32_t timeout,
	size_t& elements_acquired,
	size_t& elements_remaining)
{
	nirio_status status = NiRio_Status_Success;
	if (!_riok_proxy_ptr) return NiRio_Status_ResourceNotInitialized;

	status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_fatal(status)) return status;

	nNIRIOSRV200::tRioDeviceSocketInputParameters in = {};
	uint32_t stuffed[2];
	nNIRIOSRV200::tRioDeviceSocketOutputParameters out = {};
	initRioDeviceSocketOutputParameters(out, stuffed, sizeof(stuffed));

	in.function    = nNIRIOSRV200::nRioFunction::kFifo;
	in.subfunction = nNIRIOSRV200::nRioDeviceFifoFunction::kWait;

	in.params.fifo.channel                   = _fifo_channel;
	in.params.fifo.op.wait.elementsRequested = static_cast<uint32_t>(elements_requested);
	in.params.fifo.op.wait.scalarType        = static_cast<uint32_t>(_datatype_info.scalar_type);
	in.params.fifo.op.wait.bitWidth          = _datatype_info.width * 8;
	in.params.fifo.op.wait.output            = _fifo_direction == OUTPUT_FIFO;
	in.params.fifo.op.wait.timeout           = timeout;

	status = _riok_proxy_ptr->sync_operation(&in, sizeof(in), &out, sizeof(out));

    if (nirio_status_not_fatal(status)) {
		elements = static_cast<data_t*>(out.params.fifo.op.wait.elements.pointer);
		elements_acquired = stuffed[0];
		elements_remaining = stuffed[1];
    }

    _lock.release();
	return status;
}

template <typename data_t>
nirio_status nirio_fifo<data_t>::release(const size_t elements)
{
	nirio_status status = NiRio_Status_Success;
	if (!_riok_proxy_ptr) return NiRio_Status_ResourceNotInitialized;

	status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_fatal(status)) return status;

	nNIRIOSRV200::tRioDeviceSocketInputParameters in = {};
	nNIRIOSRV200::tRioDeviceSocketOutputParameters out = {};

	in.function    = nNIRIOSRV200::nRioFunction::kFifo;
	in.subfunction = nNIRIOSRV200::nRioDeviceFifoFunction::kGrant;

	in.params.fifo.channel           = _fifo_channel;
	in.params.fifo.op.grant.elements = static_cast<uint32_t>(elements);

    status = _riok_proxy_ptr->sync_operation(&in, sizeof(in), &out, sizeof(out));

    _lock.release();
	return status;
}

template <typename data_t>
nirio_status nirio_fifo<data_t>::read(
	data_t* buf,
	const uint32_t num_elements,
	uint32_t timeout,
	uint32_t& num_read,
	uint32_t& num_remaining)
{
	nirio_status status = NiRio_Status_Success;
	if (!_riok_proxy_ptr) return NiRio_Status_ResourceNotInitialized;

	status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_fatal(status)) return status;

	nNIRIOSRV200::tRioDeviceSocketInputParameters in = {};
	nNIRIOSRV200::tRioDeviceSocketOutputParameters out = {};
	initRioDeviceSocketOutputParameters(out, buf, num_elements * _datatype_info.width);

	in.function = nNIRIOSRV200::nRioFunction::kFifo;
	in.subfunction = nNIRIOSRV200::nRioDeviceFifoFunction::kRead;

	in.params.fifo.channel = _fifo_channel;
	in.params.fifo.op.readWithDataType.timeout = timeout;
	in.params.fifo.op.readWithDataType.scalarType = static_cast<uint32_t>(_datatype_info.scalar_type);
	in.params.fifo.op.readWithDataType.bitWidth = _datatype_info.width * 8;

	status = _riok_proxy_ptr->sync_operation(&in, sizeof(in), &out, sizeof(out));

    if (nirio_status_not_fatal(status) || status == NiRio_Status_FifoTimeout) {
		num_read = out.params.fifo.op.read.numberRead;
		num_remaining = out.params.fifo.op.read.numberRemaining;
	}

    _lock.release();
	return status;
}

template <typename data_t>
nirio_status nirio_fifo<data_t>::write(
	const data_t* buf,
	const uint32_t num_elements,
	uint32_t timeout,
	uint32_t& num_remaining)
{
	nirio_status status = NiRio_Status_Success;
	if (!_riok_proxy_ptr) return NiRio_Status_ResourceNotInitialized;

	status = _lock.acquire(FIFO_LOCK_TIMEOUT_IN_MS);
	if (nirio_status_fatal(status)) return status;

	nNIRIOSRV200::tRioDeviceSocketInputParameters in = {};
	initRioDeviceSocketInputParameters(in, buf, num_elements * _datatype_info.width);
	nNIRIOSRV200::tRioDeviceSocketOutputParameters out = {};

	in.function = nNIRIOSRV200::nRioFunction::kFifo;
	in.subfunction = nNIRIOSRV200::nRioDeviceFifoFunction::kWrite;

	in.params.fifo.channel = _fifo_channel;
	in.params.fifo.op.writeWithDataType.timeout = timeout;
	in.params.fifo.op.readWithDataType.scalarType = static_cast<uint32_t>(_datatype_info.scalar_type);
	in.params.fifo.op.readWithDataType.bitWidth = _datatype_info.width * 8;

	status = _riok_proxy_ptr->sync_operation(&in, sizeof(in), &out, sizeof(out));

    if (nirio_status_not_fatal(status) || status == NiRio_Status_FifoTimeout) {
		num_remaining = out.params.fifo.op.write.numberRemaining;
	}

    _lock.release();
	return status;
}
