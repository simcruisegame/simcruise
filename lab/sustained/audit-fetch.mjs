import fs from 'node:fs';

const output = process.env.SUSTAINED_AUDIT;
const command = process.argv[2];
const record = (entry) => fs.appendFileSync(output, JSON.stringify({
  time: Date.now(), pid: process.pid, command, ...entry,
}) + '\n');

record({event: 'helper', args: process.argv.slice(4, 6)});
const fetchSource = globalThis.fetch;
globalThis.fetch = async (resource, options) => {
  const url = new URL(typeof resource === 'string' ? resource : resource.url ?? String(resource));
  const request = {event: 'http', host: url.host, path: url.pathname};
  const start = Date.now();
  try {
    const response = await fetchSource(resource, options);
    record({...request, status: response.status, elapsed_ms: Date.now() - start,
      content_length: response.headers.get('content-length')});
    return response;
  } catch (error) {
    record({...request, status: 0, elapsed_ms: Date.now() - start, error: String(error)});
    throw error;
  }
};
