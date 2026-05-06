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
    return this.http.get<SearchResponse>('/search', { params });
  }

  getEmail(id: number): Observable<EmailDetail> {
    return this.http.get<EmailDetail>(`/emails/${id}`);
  }

  getThread(tid: string): Observable<ThreadResponse> {
    return this.http.get<ThreadResponse>(`/threads/${tid}`);
  }

  getBackupStatus(): Observable<BackupStatus> {
    return this.http.get<BackupStatus>('/backup/status');
  }

  startBackup(): Observable<unknown> {
    return this.http.post('/backup/start', {});
  }

  stopBackup(): Observable<unknown> {
    return this.http.post('/backup/stop', {});
  }
}
