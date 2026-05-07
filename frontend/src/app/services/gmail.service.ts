import { Injectable, inject } from '@angular/core';
import { HttpClient, HttpParams } from '@angular/common/http';
import { Observable } from 'rxjs';
import { SearchResponse, EmailDetail, ThreadResponse, BackupStatus } from '../models/email.model';

@Injectable({ providedIn: 'root' })
export class GmailService {
  private http = inject(HttpClient);

  search(query: string, limit = 20, mode = 'hybrid'): Observable<SearchResponse> {
    const params = new HttpParams()
      .set('q', query)
      .set('limit', limit)
      .set('mode', mode);
    return this.http.get<SearchResponse>('/api/search', { params });
  }

  getEmail(id: number): Observable<EmailDetail> {
    return this.http.get<EmailDetail>(`/api/emails/${id}`);
  }

  getThread(tid: string): Observable<ThreadResponse> {
    return this.http.get<ThreadResponse>(`/api/threads/${tid}`);
  }

  getBackupStatus(): Observable<BackupStatus> {
    return this.http.get<BackupStatus>('/api/backup/status');
  }

  startBackup(opts: Record<string, unknown> = {}): Observable<unknown> {
    return this.http.post('/api/backup/start', opts);
  }

  stopBackup(): Observable<unknown> {
    return this.http.post('/api/backup/stop', {});
  }
}
