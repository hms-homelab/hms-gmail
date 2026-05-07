export interface SearchResult {
  id: number;
  message_id: string;
  thread_id: string;
  from: string;
  subject: string;
  date: string;
  snippet: string;
  has_attachment: boolean;
  score: number;
}

export interface SearchResponse {
  results: SearchResult[];
  count: number;
  query: string;
}

export interface EmailDetail {
  id: number;
  message_id: string;
  thread_id: string;
  from: string;
  subject: string;
  date: string;
  body_text: string;
  body_html: string;
  has_attachment: boolean;
}

export interface ThreadResponse {
  thread_id: string;
  emails: SearchResult[];
  count: number;
}

export interface BackupStatus {
  state: string;
  downloaded: number;
  skipped: number;
  fetched: number;
  total: number;
  purged: number;
  current_batch: number;
  total_batches: number;
  embed_done: number;
  embed_errors: number;
  embed_total: number;
  db_total: number;
  db_embedded: number;
  db_failed: number;
  last_error: string;
  last_run: string;
  next_run: string;
}
